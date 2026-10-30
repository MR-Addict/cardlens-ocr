import nativeSdk from 'libcardlens_ocr.so';
import { common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';
import { fileIo as fs } from '@kit.CoreFileKit';

export type DocumentType = 'id_card' | 'driver_license' | 'vehicle_license';
export type DocumentSide = 'portrait' | 'emblem' | 'main';
export interface RecognizeOptions { documentType: DocumentType; side: DocumentSide; }
export interface FieldResult { value: string; rawText: string; confidence: number; polygon: number[][]; }
export interface TextLine { text: string; confidence: number; polygon: number[][]; }
export interface DocumentResult {
  documentType: DocumentType;
  side: DocumentSide;
  status: 'complete' | 'partial' | 'failed';
  width: number;
  height: number;
  rotation: number;
  fields: Record<string, FieldResult>;
  lines: TextLine[];
  warnings: string[];
}
let ready: boolean = false;
let initializing: boolean = false;
let busy: boolean = false;

async function initialize(context: common.Context): Promise<void> {
  if (ready || initializing) { throw new Error('ALREADY_INITIALIZED'); }
  initializing = true;
  try {
    const directory: string = context.filesDir + '/cardlens-ocr';
    if (!fs.accessSync(directory)) { fs.mkdirSync(directory, true); }
    const files: string[] = ['det.param', 'det.bin', 'rec.param', 'rec.bin', 'dictionary.txt'];
    for (const name of files) {
      const bytes: Uint8Array = await context.resourceManager.getRawFileContent('cardlens_ocr/' + name);
      const file: fs.File = fs.openSync(directory + '/' + name + '.tmp', fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
      try {
        const buffer: ArrayBuffer = bytes.slice().buffer as ArrayBuffer;
        const written: number = fs.writeSync(file.fd, buffer);
        if (written !== buffer.byteLength) { throw new Error('MODEL_WRITE_FAILED'); }
      } finally { fs.closeSync(file); }
      fs.renameSync(directory + '/' + name + '.tmp', directory + '/' + name);
    }
    await nativeSdk.initialize(directory);
    ready = true;
  } finally { initializing = false; }
}

async function recognize(imageBytes: ArrayBuffer, options: RecognizeOptions): Promise<DocumentResult> {
  if (!ready) { throw new Error('NOT_INITIALIZED'); }
  if (busy) { throw new Error('BUSY'); }
  if (imageBytes.byteLength < 8 || imageBytes.byteLength > 20000000) { throw new Error('INVALID_IMAGE'); }
  const bytes: Uint8Array = new Uint8Array(imageBytes);
  const jpeg: boolean = bytes[0] === 255 && bytes[1] === 216;
  const png: boolean = bytes[0] === 137 && bytes[1] === 80 && bytes[2] === 78 && bytes[3] === 71;
  if (!jpeg && !png) { throw new Error('UNSUPPORTED_IMAGE_FORMAT'); }
  busy = true;
  let source: image.ImageSource | undefined = undefined;
  let pixels: image.PixelMap | undefined = undefined;
  try {
    source = image.createImageSource(imageBytes);
    const sourceInfo: image.ImageInfo = await source.getImageInfo();
    if (sourceInfo.size.width < 64 || sourceInfo.size.height < 64 ||
        sourceInfo.size.width > 8192 || sourceInfo.size.height > 8192 ||
        sourceInfo.size.width * sourceInfo.size.height > 24000000) { throw new Error('INVALID_IMAGE'); }
    pixels = await source.createPixelMap({ desiredPixelFormat: image.PixelMapFormat.RGBA_8888 });
    let orientation: number = 1;
    try { orientation = Number.parseInt(await source.getImageProperty(image.PropertyKey.ORIENTATION)); } catch (e) { orientation = 1; }
    if (orientation === 3) { await pixels.rotate(180); }
    if (orientation === 6) { await pixels.rotate(90); }
    if (orientation === 8) { await pixels.rotate(270); }
    if (orientation === 2) { await pixels.flip(true, false); }
    if (orientation === 4) { await pixels.flip(false, true); }
    if (orientation === 5) { await pixels.rotate(90); await pixels.flip(true, false); }
    if (orientation === 7) { await pixels.rotate(90); await pixels.flip(false, true); }
    const info: image.ImageInfo = await pixels.getImageInfo();
    const stride: number = pixels.getBytesNumberPerRow();
    const rgba: ArrayBuffer = new ArrayBuffer(stride * info.size.height);
    await pixels.readPixelsToBuffer(rgba);
    const result: string = await nativeSdk.recognize(rgba, info.size.width, info.size.height, stride, options.documentType, options.side);
    return JSON.parse(result) as DocumentResult;
  } finally {
    try {
      if (pixels !== undefined) { await pixels.release(); }
    } finally {
      try { if (source !== undefined) { await source.release(); } }
      finally { busy = false; }
    }
  }
}
function release(): void {
  if (busy || initializing) { throw new Error('BUSY'); }
  nativeSdk.release();
  ready = false;
}

export interface OcrSdk {
  initialize: (context: common.Context) => Promise<void>;
  recognize: (imageBytes: ArrayBuffer, options: RecognizeOptions) => Promise<DocumentResult>;
  release: () => void;
}
const ocr: OcrSdk = { initialize: initialize, recognize: recognize, release: release };
export default ocr;
