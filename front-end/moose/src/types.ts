export interface TreeInputState {
  N: number,
  NL: number,
  kvSize: number,
  blockSize: number,
  F: number,
  bpk: number,
};

export interface TreeOutputState {
  lvl: number;
  lvlRuns: number[];
  lvlCaps: number[];
  ris: number[];
  nis: number[];
  isSucc: boolean;
  errMsg: string;
};