import { TreeOutputState } from '../../types';

interface State {
  cost: number;
  ri: number;
  ni: number;
};

const states = new Map<string, State>();
let internalDiv = 4;


export function dp(N: number, NL: number, F: number, blockSize: number, kvSize: number): TreeOutputState {
  states.clear();
  internalDiv = blockSize / kvSize;
  const fail = { 
    lvl: 0,
    lvlRuns: [],
    lvlCaps: [],
    isSucc: false,
    ris: [],
    nis: [],
    errMsg: '',
  };
  if (!dpInner(N - NL, F, NL)) {
    fail.errMsg = 'No solution found';
    return fail;
  }
  let prev = F, cap = N;
  let ris = [];
  let nis = [];
  while (cap > 0) {
    let state = getState(cap, prev);
    if (state === undefined) {
      fail.errMsg = 'unknown error';
      return fail;
    }
    let ni = state.ni;
    let ri = state.ri;
    cap -= prev * ri;
    prev = prev * ri;
    ris.push(ri);
    nis.push(ni);
  }
  if (NL !== 0) {
    let state = getState(0, prev);
    if (state === undefined) {
      fail.errMsg = 'unknown error';
      return fail;
    }
    let ni = state.ni;
    let ri = state.ri;
    ris.push(ri);
    nis.push(ni);
  }
  prev = F;
  let caps = [];
  let levelRuns = []
  for (let i = 0; i < ris.length; i++) {
    let ri = ris[i];
    let ni = nis[i];
    let lvlSize = prev * ri;
    caps.push(lvlSize);
    let runSize = Math.round(lvlSize / ni);
    for (let j = 0; j < ni; j++) {
      levelRuns.push(runSize);
    }
    prev = lvlSize;
  }
  const ret: TreeOutputState = {
    lvl: ris.length,
    lvlRuns: levelRuns,
    lvlCaps: caps,
    isSucc: true,
    ris: ris,
    nis: nis,
    errMsg: '',
  };
  return ret;
}

function getCost(cap: number, prev: number) {
  const key = `${cap}-${prev}`;
  const state = states.get(key);
  return state ? state.cost : -1;
}

function getState(cap: number, prev: number) {
  const key = `${cap}-${prev}`;
  return states.get(key);
}

function setCost(cap: number, prev: number, cost: number, ri: number, ni: number) {
  const key = `${cap}-${prev}`;
  states.set(key, { cost, ri, ni });
}

function calCost(ri: number, ni: number) {
  return ni + 16 / internalDiv + (ri - 1) / internalDiv / ni;
}

function dpInner(cap: number, prev: number, NL: number) {
  if (getCost(cap, prev) !== -1) {
    return true;
  }
  if (cap <= 0) {
    if (NL !== 0 && prev >= NL) {
      return false;
    }
    if (NL === 0) {
      setCost(0, prev, 0, 0, 0);
      return true;
    }
    const ri = Math.ceil(cap / prev);
    const ni = Math.round(Math.sqrt(ri));
    setCost(0, prev, calCost(ri, ni), ri, ni);
    return true;
  }
  if (cap < prev) {
    return false;
  }
  const t = Math.ceil(cap / prev);
  if (!dpInner(cap - prev * t, t * prev, NL)) {
    return false;
  }
  let ni = Math.round(Math.sqrt(t));
  const cost = calCost(t, ni) + getCost(cap - prev * t, t * prev);
  setCost(cap, prev, cost, t, ni);
  for (let c = 2; c < t; c++) {
    const nxtCap = cap - c * prev;
    const nxtPrev = c * prev;
    if (dpInner(nxtCap, nxtPrev, NL)) {
      ni = Math.round(Math.sqrt(c));
      const cost = calCost(c, ni) + getCost(nxtCap, nxtPrev);
      if (cost < getCost(cap, prev)) {
        setCost(cap, prev, cost, c, ni);
      }
    }
  }
  return true;
}