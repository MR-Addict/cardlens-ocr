import ocr from '../../out/packages/cardlens-ocr/node.js';
await ocr.initialize();
try {
  console.log(await ocr.recognize(process.argv[2], { documentType: 'id_card', side: 'portrait' }));
} finally { await ocr.release(); }
