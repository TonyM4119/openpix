import sys
# Pad the image to a 1 KB multiple with 0xFF (erased-flash value). The stock
# HolidayCoro updater writes in 4-byte words and can drop a short tail.
p=sys.argv[1]; d=open(p,'rb').read()
n=(len(d)+1023)//1024*1024
open(p,'wb').write(d+b'\xff'*(n-len(d)))
print('padded',len(d),'->',n)
