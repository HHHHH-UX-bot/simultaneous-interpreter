"""Embed the already validated integer model; never include enrolled profiles."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--model',type=Path,default=ROOT/'assets/speaker_cnn/model.bin');parser.add_argument('--manifest',type=Path,default=ROOT/'assets/speaker_cnn/manifest.json');args=parser.parse_args()
 blob=args.model.read_bytes();record=json.loads(args.manifest.read_text(encoding='utf-8'))
 (ROOT/'output/speaker_cnn').mkdir(parents=True,exist_ok=True)
 assert hashlib.sha256(blob).hexdigest()==record['model_sha256'];words=struct.unpack('<8192I',blob);assert words[:3]==(0x314b5053,1,7076)
 checksum=2166136261
 for value in words[:7076]:checksum=((checksum^value)*16777619)&0xffffffff
 header=ROOT/'software/network_ui/speaker_weights.h'
 header.write_text('#ifndef SPEAKER_WEIGHTS_H\n#define SPEAKER_WEIGHTS_H\n#include <stdint.h>\n#define SPEAKER_MODEL_WORDS 7076u\n#define SPEAKER_MODEL_ID 0x%08xu\n#define SPEAKER_MODEL_FNV 0x%08xu\nextern const uint32_t speaker_model_words[SPEAKER_MODEL_WORDS];\n#endif\n'%(words[3],checksum))
 target=ROOT/'output/speaker_cnn/model_weights.c';target.write_text('/* Public-audio trained CNN parameters; no user profiles. */\n#include "../../software/network_ui/speaker_weights.h"\nconst uint32_t speaker_model_words[SPEAKER_MODEL_WORDS]={\n'+''.join(','.join('0x%08xu'%v for v in words[a:min(a+8,7076)])+',\n' for a in range(0,7076,8))+'};\n')
 result=dict(model_sha256=record['model_sha256'],model_id=words[3],fnv32=checksum,used_words=7076,c_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),runtime_profiles_included=False)
 (ROOT/'output/speaker_cnn/firmware_weights.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
