export const initialize: (directory: string) => Promise<void>;
export const recognize: (rgba: ArrayBuffer, width: number, height: number, stride: number, type: string, side: string) => Promise<string>;
export const release: () => void;
