import tensorflow as tf
from tensorflow.keras import backend as K
K.set_image_data_format("channels_first")
from attention import MultiHeadSelfAttention2D
from transformer import Transformer
from layers import Residual
import os

class ChessCoachModel:
  
  board_side = 8
  input_planes_count = 101
  output_planes_count = 73
  residual_count = 10
  filter_count = 128
  attention_heads = 8
  dense_count = 256
  weight_decay = 1e-4

  transformer_layers = 4 # 6
  transformer_filters = 128 # 512
  transformer_heads = 8
  transformer_feedforward = 512 # 2048
  transformer_dropout_rate = 0.1
  transformer_num_words = 5000 # Arbitrary for now
  transformer_max_length = 128 # Arbitrary for now

  input_name = "Input"
  output_value_name = "OutputValue"
  output_mcts_value_name = "OutputMctsValue"
  output_policy_name = "OutputPolicy"
  output_reply_policy_name = "OutputReplyPolicy"
  output_commentary_encoder_name = "OutputCommentaryEncoder"
  
  token_start = "<start>"
  token_end = "<end>"
  token_unk = "<unk>"
  token_pad = "<pad>"

  def __init__(self):
    self.architecture_layers = {
      "conv2d": self.conv2d_layer,
      "attention": self.attention_layer,
      "augmented": self.augmented_layer,
    }

  def conv2d_layer(self, name):
    return tf.keras.layers.Conv2D(filters=self.filter_count, kernel_size=(3,3), strides=1, padding="same", data_format="channels_first",
      use_bias=False, kernel_initializer="he_normal", kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay), name=name)
  
  def attention_layer(self, name):
    return MultiHeadSelfAttention2D(total_depth=self.filter_count, num_heads=self.attention_heads, weight_decay=self.weight_decay, name=name)
  
  def augmented_layer(self, name):
    attention = MultiHeadSelfAttention2D(total_depth=self.filter_count // 2, num_heads=self.attention_heads, weight_decay=self.weight_decay, name=name + "/attention")
    conv2d = tf.keras.layers.Conv2D(filters=self.filter_count // 2, kernel_size=(3,3), strides=1, padding="same", data_format="channels_first",
      use_bias=False, kernel_initializer="he_normal", kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay), name=name + "/conv2d")
    return lambda x: tf.concat([attention(x), conv2d(x)], axis=1)

  def stem(self, x):
    # Initial convolutional layer
    x = tf.keras.layers.Conv2D(filters=self.filter_count, kernel_size=(3,3), strides=1, padding="same", data_format="channels_first",
      name=f"initial/conv2d_{self.filter_count}",
      use_bias=False, kernel_initializer="he_normal", kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay))(x)
    x = tf.keras.layers.BatchNormalization(axis=1, name=f"initial/batchnorm")(x)
    x = tf.keras.layers.ReLU(name=f"initial/relu")(x)
    return x

  def tower(self, x, config):
    # Residual layers
    architecture = config.training_network["architecture"]
    architecture_layer = self.architecture_layers[architecture]
    residual = Residual([architecture_layer, architecture_layer], [architecture, architecture], self.filter_count, self.weight_decay)
    for i in range(self.residual_count):
      x = residual.build_residual_block_v2(x, i)

    # Tower BN/ReLU
    x = tf.keras.layers.BatchNormalization(axis=1, name=f"tower/batchnorm")(x)
    x = tf.keras.layers.ReLU(name=f"tower/relu")(x)
    return x

  def value_head(self, x, name, output_name):
    value_filter_count = 1
    x = tf.keras.layers.Conv2D(filters=value_filter_count, kernel_size=(1,1), strides=1, data_format="channels_first",
      name=f"{name}/conv2d_{value_filter_count}",
      use_bias=False, kernel_initializer="he_normal", kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay))(x)
    x = tf.keras.layers.BatchNormalization(axis=1, name=f"{name}/batchnorm")(x)
    x = tf.keras.layers.ReLU(name=f"{name}/relu")(x)
    x = tf.keras.layers.Flatten(name=f"{name}/flatten")(x)
    # Add bias for these layers with no more batchnorms.
    x = tf.keras.layers.Dense(self.dense_count, kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay), activation="relu", use_bias=True,
      name=f"{name}/dense_{self.dense_count}")(x)
    x = tf.keras.layers.Dense(1, kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay), activation="tanh", use_bias=True,
      name=output_name)(x)
    return x

  def policy_head(self, x, name, output_name, config):
    architecture = config.training_network["architecture"]
    architecture_layer = self.architecture_layers[architecture]
    x = architecture_layer(name=f"{name}/{architecture}_{self.filter_count}")(x)
    x = tf.keras.layers.BatchNormalization(axis=1, name=f"{name}/batchnorm")(x)
    x = tf.keras.layers.ReLU(name=f"{name}/relu")(x)
    # Add bias for these layers with no more batchnorms.
    policy = tf.keras.layers.Conv2D(filters=self.output_planes_count, kernel_size=(1,1), strides=1, data_format="channels_first",
      use_bias=True, kernel_initializer="he_normal", kernel_regularizer=tf.keras.regularizers.l2(self.weight_decay), name=output_name)(x)
    return policy

  def commentary_encoder_head(self, x, name, output_name):
    # Just reshape to 1D-sequence, channels-last.
    x = tf.reshape(x, [-1, self.filter_count, self.board_side * self.board_side])
    encoder = tf.transpose(x, [0, 2, 1], name=output_name)
    return encoder

  def build(self, config):
    input = tf.keras.layers.Input(shape=(self.input_planes_count, self.board_side, self.board_side), dtype="float32", name=self.input_name)

    # Stem
    stem = self.stem(input)

    # Residual tower
    tower = self.tower(stem, config)

    # Value heads
    value = self.value_head(tower, "value", self.output_value_name)
    mcts_value = self.value_head(tower, "mcts_value", self.output_mcts_value_name)

    # Policy heads
    policy = self.policy_head(tower, "policy", self.output_policy_name, config)
    reply_policy = self.policy_head(tower, "reply_policy", self.output_reply_policy_name, config)

    # Commentary encoder head
    commentary_encoder = self.commentary_encoder_head(tower, "commentary_encoder", self.output_commentary_encoder_name)

    return tf.keras.Model(input, [value, mcts_value, policy, reply_policy, commentary_encoder])

  def subset_play(self, model):
    return tf.keras.Model(model.input, model.outputs[:4])

  def subset_commentary_encoder(self, model):
    return tf.keras.Model(model.input, model.outputs[4:])

  def build_commentary_decoder(self, config, tokenizer=None):
    if not tokenizer:
      vocabulary_path = os.path.join(config.training_network["commentary_path_training"], config.training_network["vocabulary_filename"])
      with open(vocabulary_path, 'r', errors="ignore") as f:
        comments = f.readlines()
      comments = [f"{self.token_start} {c} {self.token_end}" for c in comments]

      tokenizer = tf.keras.preprocessing.text.Tokenizer(num_words=self.transformer_num_words, oov_token=self.token_unk, filters='!"#$%&()*+.,-/:;=?@[\\]^_`{|}~ ')
      tokenizer.fit_on_texts(comments)
      tokenizer.word_index[self.token_pad] = 0
      tokenizer.index_word[0] = self.token_pad

    vocab_size = self.transformer_num_words + 1
    max_length = self.transformer_max_length

    transformer = Transformer(self.transformer_layers, self.transformer_filters, self.transformer_heads,
      self.transformer_feedforward, vocab_size, max_length, self.transformer_dropout_rate)

    return transformer, tokenizer