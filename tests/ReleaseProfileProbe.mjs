import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const read=f=>fs.readFileSync(path.join(root,f),'utf8');
assert.match(read('CMakeLists.txt'),/project\(EinzigViewer VERSION 1\.0\.0\b/,'Release version must be 1.0.0');
if(process.argv.includes('--complete')) {
  assert.match(read('README.md'),/AI 功能仍在測試與改進中/);
  assert.match(read('README.en.md'),/AI is still being tested and improved/);
  assert.match(read('README.txt'),/AI 功能仍在測試與改進中/);
  assert.match(read('scripts/Package-Release.ps1'),/models-se/,'Packaging must include AI models');
}
console.log('PASS: release version'+(process.argv.includes('--complete')?', AI disclosure and package prerequisites.':'.'));
