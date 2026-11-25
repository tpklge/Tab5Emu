"""Fixed-build SD overlay descriptors; no dynamic linking or arbitrary modules."""
import hashlib
import struct
MAGIC=b'T5SDOVL1'
HEADER=struct.Struct('<8sII32s')


def descriptor(payload,address,maximum=8*1024*1024):
    if not payload or len(payload)>maximum or not 0x48000000<=address<address+len(payload)<=0x4a000000:
        raise ValueError('invalid core bounds')
    return HEADER.pack(MAGIC,len(payload),address,hashlib.sha256(payload).digest())


def verify(header,payload,address):
    if len(header)!=HEADER.size:raise ValueError('invalid core descriptor')
    magic,size,target,sha=HEADER.unpack(header)
    if magic!=MAGIC or size!=len(payload) or target!=address or hashlib.sha256(payload).digest()!=sha:
        raise ValueError('core does not match this firmware')
    return sha.hex()
