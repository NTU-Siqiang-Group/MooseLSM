export interface TreeInputState {
  N: number,
  NL: number,
  kvSize: number,
  blockSize: number,
  F: number,
  bpk: number,
};

export interface TreeOutputState {
  lvl?: number;
  lvlRuns?: number[];
  lvlCaps?: number[];
  isSucc?: boolean;
  errMsg?: string;
};