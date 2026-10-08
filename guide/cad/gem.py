"""One Gemini image call: python3 gem.py <model> <out.png> <prompt> [reference.png ...]

The key comes from GEMINI_API_KEY, or from GEMINI_API_KEY= in ~/Dev/ai-scoring/.env. Exits non-zero on any failure."""
import base64, json, os, sys, urllib.error, urllib.request

def key():
    if os.environ.get('GEMINI_API_KEY'):
        return os.environ['GEMINI_API_KEY']
    env = os.path.expanduser('~/Dev/ai-scoring/.env')
    for line in open(env) if os.path.exists(env) else []:
        if line.startswith('GEMINI_API_KEY='):
            return line.split('=', 1)[1].strip().strip('"')
    sys.exit('GEMINI_API_KEY is not set')

model, out, prompt = sys.argv[1], sys.argv[2], sys.argv[3]
parts = [{'text': prompt}] + [{'inline_data': {'mime_type': 'image/png', 'data': base64.b64encode(open(r, 'rb').read()).decode()}} for r in sys.argv[4:]]
body = {'contents': [{'parts': parts}], 'generationConfig': {'responseModalities': ['IMAGE'], 'imageConfig': {'aspectRatio': '3:2'}}}
req = urllib.request.Request(f'https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent', data=json.dumps(body).encode(),
                             headers={'Content-Type': 'application/json', 'x-goog-api-key': key()})
try:
    r = json.load(urllib.request.urlopen(req, timeout=280))
except urllib.error.HTTPError as e:
    print('HTTP', e.code, e.read()[:400].decode()); sys.exit(1)
for c in r.get('candidates', []):
    for p in c['content']['parts']:
        d = p.get('inlineData') or p.get('inline_data')
        if d:
            open(out, 'wb').write(base64.b64decode(d['data'])); print('OK', out); sys.exit(0)
print('NOIMG', json.dumps(r)[:400]); sys.exit(1)
