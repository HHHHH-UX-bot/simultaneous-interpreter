"""Training normalization is folded into the existing int8 FPGA convolutions."""
import torch
from torch import nn
from torch.nn import functional as F
from speaker_cnn_model import SpeakerCNN,weight_quantization,fake_floor,ACTIVATION_SCALE
class SpeakerCNNBN(SpeakerCNN):
 def __init__(self,classes):
  super().__init__(classes)
  self.bn1=nn.BatchNorm2d(16);self.bn2=nn.BatchNorm2d(32);self.bn3=nn.BatchNorm2d(64)
  nn.init.zeros_(self.fc.bias);self.margin=0.
 def batchnorm(self,module):
  return self.bn1 if module is self.conv1 else self.bn2 if module is self.conv2 else self.bn3
 def deployment_params(self,module):
  if module is self.fc:return module.weight,module.bias
  bn=self.batchnorm(module);factor=bn.weight/torch.sqrt(bn.running_var+bn.eps)
  return module.weight*factor[:,None,None,None],(module.bias-bn.running_mean)*factor+bn.bias
 def layer(self,x,module):
  if not self.quantized:return F.relu(self.batchnorm(module)(module(x)))
  weight,bias=self.deployment_params(module);w,shift=weight_quantization(weight);scale=ACTIVATION_SCALE*2.**(-shift)
  b=bias+(torch.round(bias/scale)*scale-bias).detach()
  return fake_floor(F.relu(F.conv2d(x,w,b,stride=2,padding=1)),ACTIVATION_SCALE,0,127)
 def forward(self,x,label=None):
  cosine=self.embed(x)@F.normalize(self.classifier,dim=1).T
  if label is not None:
   cosine=cosine.clone();cosine[torch.arange(len(label)),label]-=self.margin
  return cosine*20
