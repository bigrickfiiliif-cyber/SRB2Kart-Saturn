// Builds Blockwerk.html: inlines the PS2 runtime sources into editor.html.
// Usage: node build.mjs
import { readFileSync, writeFileSync } from 'node:fs';
const dir = new URL('.', import.meta.url);
let html = readFileSync(new URL('editor.html', dir), 'utf8');
html = html.replace(/@@([\w./-]+)@@/g, (_, f) => {
  const src = readFileSync(new URL(f, dir), 'utf8');
  if (/<\/script/i.test(src)) throw new Error(f + ' contains </script');
  return '\n' + src;
});
writeFileSync(new URL('Blockwerk.html', dir), html);
console.log('Blockwerk.html', (html.length / 1024).toFixed(0) + ' KB');
