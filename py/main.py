from __future__ import absolute_import, division, print_function, unicode_literals

import storage
import os

def test_train():
  import network
  from model import ChessCoachModel
  import numpy
  batch_size = network.config.training_network["batch_size"]
  images = numpy.zeros((batch_size, ChessCoachModel.input_planes_count, ChessCoachModel.board_side, ChessCoachModel.board_side), dtype=numpy.float32)
  values = numpy.full((batch_size, 1), 0.123, dtype=numpy.float32)
  policies = numpy.zeros((batch_size, ChessCoachModel.output_planes_count, ChessCoachModel.board_side, ChessCoachModel.board_side), dtype=numpy.float32)
  reply_policies = numpy.zeros((batch_size, ChessCoachModel.output_planes_count, ChessCoachModel.board_side, ChessCoachModel.board_side), dtype=numpy.float32)
  for i in range(batch_size):
    policies[i][1][2][3] = 1.0

  #network.config.training_network["validation_interval"] = 1
  network.load_network(network.config.training_network["name"])
  network.train_batch(step=1, images=images, values=values, policies=policies, reply_policies=reply_policies)

def test_predict():
  import network
  from model import ChessCoachModel
  import numpy
  batch_size = network.config.training_network["batch_size"]
  images = numpy.zeros((batch_size, ChessCoachModel.input_planes_count, ChessCoachModel.board_side, ChessCoachModel.board_side), dtype=numpy.float32)

  network.load_network(network.config.training_network["name"])
  network.predict_batch(images)

print("Starting Python-ChessCoach")
test_train()
#test_predict()