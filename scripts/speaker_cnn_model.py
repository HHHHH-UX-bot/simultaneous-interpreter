"""Small speaker embedding CNN and deployment-compatible power-of-two QAT."""
import math
import torch
from torch import nn
from torch.nn import functional as F
ACTIVATION_SCALE=1/32
def weight_quantization(w):
 shift=max(0,min(15,int(math.floor(math.log2(127/max(float(w.detach().abs().max()),1e-8))))))
 scale=2.**(-shift);q=torch.round(w/scale).clamp(-127,127)
 return w+(q*scale-w).detach(),shift
def fake_floor(x,scale,low,high):
 q=torch.floor(x/scale).clamp(low,high)*scale
 return x+(q-x).detach()
class SpeakerCNN(nn.Module):
 def __init__(self,classes):
  super().__init__();self.conv1=nn.Conv2d(1,16,3,2,1);self.conv2=nn.Conv2d(16,32,3,2,1);self.conv3=nn.Conv2d(32,64,3,2,1)
  self.fc=nn.Linear(64,64);self.classifier=nn.Parameter(torch.randn(classes,64)*.1);self.quantized=False
 def layer(self,x,module):
  if not self.quantized:return F.relu(module(x))
  w,shift=weight_quantization(module.weight);scale=ACTIVATION_SCALE*2.**(-shift)
  b=module.bias+(torch.round(module.bias/scale)*scale-module.bias).detach()
  y=F.conv2d(x,w,b,stride=2,padding=1)
  return fake_floor(F.relu(y),ACTIVATION_SCALE,0,127)
 def embed(self,x):
  for module in (self.conv1,self.conv2,self.conv3):x=self.layer(x,module)
  x=x.mean(dim=(2,3))
  if self.quantized:
   x=fake_floor(x,ACTIVATION_SCALE,0,127)
   w,shift=weight_quantization(self.fc.weight);scale=ACTIVATION_SCALE*2.**(-shift)
   b=self.fc.bias+(torch.round(self.fc.bias/scale)*scale-self.fc.bias).detach()
   x=fake_floor(F.linear(x,w,b),scale,-32768,32767)
  else:x=self.fc(x)
  return F.normalize(x,dim=1,eps=1e-9)
 def forward(self,x,label=None):
  embedding=self.embed(x);cosine=embedding@F.normalize(self.classifier,dim=1).T
  if label is not None:
   cosine=cosine.clone();cosine[torch.arange(len(label)),label]-=.2
  return cosine*20
