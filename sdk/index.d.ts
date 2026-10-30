export type RecognizeOptions =
  | { documentType: 'id_card'; side: 'portrait' | 'emblem' }
  | { documentType: 'driver_license' | 'vehicle_license'; side: 'main' };
export interface FieldResult { value: string; rawText: string; confidence: number; polygon: number[][]; }
export interface TextLine { text: string; confidence: number; polygon: number[][]; }
export interface DocumentResult {
  documentType: RecognizeOptions['documentType'];
  side: RecognizeOptions['side'];
  status: 'complete' | 'partial' | 'failed';
  width: number; height: number; rotation: number;
  fields: Record<string, FieldResult>;
  lines: TextLine[];
  warnings: string[];
}
export interface InitializeOptions {
  modelBaseUrl?: string;
  modelDirectory?: string;
  onProgress?: (progress: { loaded: number; total: number }) => void;
}
declare const ocr: {
  initialize(options?: InitializeOptions): Promise<void>;
  recognize(image: Blob | ArrayBuffer | Uint8Array | string | URL, options: RecognizeOptions): Promise<DocumentResult>;
  release(): Promise<void>;
};
export default ocr;
