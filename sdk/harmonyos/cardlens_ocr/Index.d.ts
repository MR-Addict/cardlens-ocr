import { common } from '@kit.AbilityKit';
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
export interface OcrSdk {
  initialize: (context: common.Context) => Promise<void>;
  recognize: (imageBytes: ArrayBuffer, options: RecognizeOptions) => Promise<DocumentResult>;
  release: () => void;
}
declare const ocr: OcrSdk;
export default ocr;
