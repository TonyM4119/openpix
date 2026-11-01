import gzip,sys
src,dst=sys.argv[1],sys.argv[2]
data=gzip.compress(open(src,'rb').read(),9,mtime=0)
with open(dst,'w') as f:
    f.write('// generated from web/index.html\n#pragma once\n#include <stdint.h>\n')
    f.write('static const uint8_t web_index_gz[%d] = {\n'%len(data))
    for i in range(0,len(data),20): f.write(','.join(str(b) for b in data[i:i+20])+',\n')
    f.write('};\n')
print('web gz',len(data))
