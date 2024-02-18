import * as React from 'react';
import { Button, InputNumber, Space, Layout } from 'antd';

import { TreeInputState, TreeOutputState } from '../../types';
import TreeStructure from './TreeStructure';
import { dp } from './algm'; 

import './tree.less';

const { Content } = Layout;

const defaultN = 22000000;
const defaultNL = 0;
const defaultKVSize = 1024;
const defaultBlockSize = 4096;
const defaultF = 2 << 20;
const defaultBPK = 5;

interface TreeState {
  inputState: TreeInputState;
  outputState: TreeOutputState;
};

class Tree extends React.Component<{}, TreeState> {
  constructor(props: any) {
    super(props);
    this.state = {
      inputState: {
        N: defaultN,
        NL: defaultNL,
        kvSize: defaultKVSize,
        blockSize: defaultBlockSize,
        F: defaultF,
        bpk: defaultBPK,
      },
      outputState: {
        lvl: 0,
        lvlRuns: [],
        lvlCaps: [],
        isSucc: false,
      },
    }
  }


  computeStructure = () => {
    const { N, NL, kvSize, blockSize, F, bpk } = this.state.inputState;
    const cap = N * kvSize;
    const NLCap = NL * kvSize;
    this.setState({ ...this.state, outputState: dp(cap, NLCap, F, blockSize, kvSize) });
    console.log(this.state.outputState);
  }

  bindInputValue = (key: string) => {
    return (e: number | null) => {
      this.setState({ ...this.state, inputState: { ...this.state.inputState, [key]: e === null ? 0 : e } });
    };
  }

  render() {
    return (
      <Content id="tree-compute-component">
        <Space direction='vertical' style={{ width: '50%' }}>
          <Space direction='horizontal' align='start' style={{ display: "flex", marginTop: '0.5rem' }}>
            <Space direction='vertical' size='large' style={{ display: "flex" }}>
              <Content className="tree-setting-class">1. Dataset</Content>
              <Content className="N-block">
                <InputNumber controls={false} onChange={this.bindInputValue('N')}  
                  addonBefore="# Entries" id="N-input" defaultValue={defaultN} />
              </Content>
              <Content className="N-block">
                <InputNumber controls={false} onChange={this.bindInputValue('kvSize')}
                  addonBefore='key-value size (Bytes)' id="kv-input" defaultValue={defaultKVSize} />
              </Content>
            </Space>
            <Space direction='vertical' style={{ display: 'flex'}} size='large'>
              <Content className="tree-setting-class" style={{ textAlign: 'center' }}>2. Running Environment</Content>
              <InputNumber controls={false} style={{ textAlign: 'right' }} 
                onChange={this.bindInputValue('blockSize')}
                addonBefore='Page Size' id='B-input' defaultValue={defaultBlockSize} />
            </Space>
          </Space>
          <Space direction='horizontal' align='start' style={{ display: "flex", marginTop: '0.5rem' }}>
            <Space direction='vertical' size='large' style={{ display: "flex" }}>
              <Content className="tree-setting-class">3. Main Memory Allocation</Content>
              <InputNumber controls={false} style={{ textAlign: 'right' }} onChange={this.bindInputValue('F')} addonBefore='Buffer Size (Bytes)' id='M-input' defaultValue={defaultF} />
              <InputNumber controls={false} style={{ textAlign: 'right' }} onChange={this.bindInputValue('bpk')} addonBefore='Bloom Filter (bits-per-key)' id='M-input' defaultValue={defaultBPK} />
            </Space>
            <Space direction='vertical' size='large' style={{ display: "flex" }}>
              <Content className="tree-setting-class">4. Tree Structure</Content>
              <InputNumber controls={false} onChange={this.bindInputValue('NL')} style={{ textAlign: 'right' }}
                addonBefore='# Entries at last level' id="NL-input" defaultValue={defaultNL} />
            </Space>
          </Space>
          <Button type='primary' size='large' style={{ width: '100%', marginTop: '1rem' }} onClick={this.computeStructure}>Compute</Button>
        </Space>
        <TreeStructure {...this.state.outputState} />
      </Content>
    );
  }
}

export default Tree;