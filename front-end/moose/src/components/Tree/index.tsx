import * as React from 'react';
import { Button, InputNumber, Space, Layout, Spin } from 'antd';

import { TreeInputState, TreeOutputState } from '../../types';
import TreeStructure from './TreeStructure';
import { dp } from './algm';
import axios from 'axios';
import MooseFooter from '../Footer';

import './tree.less';
import { Header } from 'antd/es/layout/layout';

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
  isLoading: boolean;
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
        ris: [],
        nis: [],
        isSucc: false,
        errMsg: '',
      },
      isLoading: false,
    }
  }


  computeStructure = async () => {
    const { N, NL, kvSize, blockSize, F, bpk } = this.state.inputState;
    const cap = N * kvSize;
    const NLCap = NL * kvSize;


    try {
      // 在这个请求中单独设置配置
      const response = await axios.post('http://172.21.47.236:1080/api/dp',

        {
          n: N * kvSize,
          nl: NL,
          f: F,
          blocksize: blockSize,
          kvsize: kvSize
        },

        {
          withCredentials: true, // 允许跨域
        });

      // try {
      //   // Make the API call to the backend
      //   axios.defaults.withCredentials = true;  //允许跨域
      //   //Content-type 响应头
      //   axios.defaults.headers['Content-type']='application/x-www-form-urlencoded; charset=UTF-8';
      //   // axios.defaults.headers['Access-Control-Allow-Origin']='*';
      //   const response = await axios.post('http://172.21.47.236:1080/api/dp', {
      //     n: N, 
      //     nl: NL, 
      //     f: F, 
      //     blocksize: blockSize, 
      //     kvsize: kvSize
      //   },
      // {headers: {
      //   'Content-Type': 'application/x-www-form-urlencoded'
      // },}

      // );

      // Assuming the backend responds with the result in the same structure as the previous outputState
      this.setState({ ...this.state, outputState: response.data, isLoading: false });
    } catch (error) {
      console.error('Failed to compute structure:', error);
      // Handle error (e.g., by setting an error message in state)
      this.setState({ ...this.state, isLoading: false });
    }
    // this.setState({ ...this.state, outputState: dp(cap, NLCap, F, blockSize, kvSize), isLoading: false });
    // console.log(this.state.outputState);
  }

  startServer = async () => {
    const { lvlRuns, nis } = this.state.outputState;
    const { F, kvSize, bpk } = this.state.inputState;
    try {
      // Make the API call to the backend
      const response = await axios.post('http://172.21.47.236:1080/api/create', {
        lvlruns: lvlRuns,
        nis: nis,
        f: F,
        kvsize: kvSize,
        bpk: bpk,
      });

      // Assuming the backend responds with the result in the same structure as the previous outputState
      console.log('Server response:', response.data);
    } catch (error) {
      console.error('Failed to start server:', error);
      // Handle error (e.g., by setting an error message in state)
    }

  }

  bindInputValue = (key: string) => {
    return (e: number | null) => {
      this.setState({ ...this.state, inputState: { ...this.state.inputState, [key]: e === null ? 0 : e } });
    };
  }

  render() {
    const treeStructure = {
      ...this.state.outputState,
      kvSize: this.state.inputState.kvSize,
      blockSize: this.state.inputState.blockSize,
    }
    return (
      <Content style={{ backgroundColor: 'white', paddingTop: '1rem' }}>
        <Space id="title" direction='vertical' style={{ fontFamily: "'Indie Flower', cursive", fontSize: "3rem", backgroundColor: '#badfca' }}>
          <Content>Structural Designs <u>M</u>eet <u>O</u>ptimality:</Content>
          <Content>Exploring <u>O</u>ptimized LSM-tree <u>S</u>tructures in A Colossal Configuration Spac<u>e</u></Content>
          <Content id="moose-img">
            <img src='supermoose.png' />
          </Content>
        </Space>
        <Space id="tree-compute-component" direction='vertical' style={{ display: "flex" }} align='center'>
          <Header style={{ fontSize: '3rem', fontWeight: 'bold', textAlign: 'center', backgroundColor: 'white' }}>Compute Your LSM-tree on-the-fly</Header>
          <Content style={{ display: 'flex', justifyContent: 'center', fontSize: '20px' }}>
            <div style={{ width: '60%' }}>
              <i>
                Configure the settings below according to your own environment and requirements. The calculator below simulates the algorithm stated above and generates a preview of the LSM-tree that meets your demand.
              </i>
              <div style={{ marginBottom: '0.5rem' }}></div>
              <br />
              The <b>Dataset</b> option indicates the intrinsic properties of the data to be inserted into the LSM-tree.
              The <b>Running Environment</b> option indicates the hardware and software environment where the LSM-tree is to be deployed. The page size is the size of I/O block in the storage system.
              The <b>Main Memory Allocation</b> option sets the a specific amount of memory to the write buffer and the bloom filter.
              The <b>Tree Structure</b> option allows you to specify the number of entries at the last level of the LSM-tree (i.e., NL).
            </div>
          </Content>
          <Space direction='vertical' style={{}}>
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
              <Space direction='vertical' style={{ display: 'flex' }} size='large'>
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
            {/* step 1 */}
            <Button type='primary' size='large' style={{ width: '100%', marginTop: '1rem' }} onClick={() => {
              this.setState({ ...this.state, isLoading: true });
              setTimeout(() => this.computeStructure(), 0.5);
            }}>Compute</Button>
            <Button type='primary' size='large' style={{ width: '100%', marginTop: '1rem' }} onClick={() => {
              this.setState({ ...this.state, isLoading: true });
              setTimeout(() => this.startServer(), 0.5);
            }}>Start Server</Button>
          </Space>
        </Space>
        {
          this.state.isLoading ?
            <Spin style={{ paddingTop: '1rem', paddingBottom: '1rem' }} spinning={this.state.isLoading} />
            :
            <TreeStructure {...treeStructure} />
        }
        <MooseFooter />
      </Content>


    );
  }
}

export default Tree;