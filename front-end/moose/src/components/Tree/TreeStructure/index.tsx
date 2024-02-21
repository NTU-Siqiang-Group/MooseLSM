import * as React from 'react';

import { Result, Space, Slider, Tag } from 'antd';
import { TreeOutputState } from '../../../types';
import { Content } from 'antd/es/layout/layout';

import './structure.less';

interface TreeStructureState {
  k: number;
}

class TreeStructure extends React.Component<TreeOutputState, TreeStructureState> {
  constructor(props: TreeOutputState) {
    super(props);
    this.state = {
      k: 1,
    };
  }

  render() {
    const step = this.props.ris.length <= 10 ? Math.floor(10 / this.props.ris.length) : 1;
    const classNames: string[] = [];
    let idx = 1;
    this.props.ris.forEach(() => {
      classNames.push(`lvl-${idx}`);
      idx += step;
      idx = Math.min(idx, 10);
    });
    const maxWidth = 30;
    const minWidth = 10;
    const lvlWidths = this.props.ris.map((_, i) => {
      return minWidth + (maxWidth - minWidth) * (i / this.props.ris.length);
    });
    const runNumbers = this.props.nis.map((ni, i): number => {
      return Math.round(Math.min(Math.max(1, ni * this.state.k), this.props.ris[i]));
    });
    const maxRi = Math.max(...this.props.ris);
    const minK = Number((1 / maxRi).toFixed(2));
    const maxK = Number((maxRi / Math.max(...this.props.nis)).toFixed(2));
    const { isSucc, errMsg } = this.props;
    return (
      <Content style={{ paddingTop: '1rem', paddingBottom: '1rem', backgroundColor: 'white' }}>
          {isSucc === false ?
            // <Result 
            //   status={errMsg.length === 0 ? "info" : "warning"}
            //   title={errMsg}
            // >
            // </Result>
            errMsg.length === 0 ?
              <Content style={{ fontSize: '20px' }}>Click Compute and Generate a Preview of your LSM-tree!</Content>
              :
              <Result
                status="warning"
                title={errMsg}
              >
              </Result>
             :
            <Space direction='vertical' style={{display: "flex" }} align='center' id="tree-wrapper">
              {
                this.props.ris.map((_, i) => {
                  return (
                    <Space style={{ display: "flex", width: `${lvlWidths[i]}%` }} direction='vertical' align='center'>
                      <Content>Capacity: {this.props.lvlCaps[i]}, run numbers: {runNumbers[i]}</Content>
                      <Space direction='horizontal' style={{ display: "flex" }} className="lvl-wrapper">
                      {
                        Array.from({length: runNumbers[i]}, (v, j) => j).map(() => {
                          return (
                            <Content style={{ width: `${lvlWidths[i] / runNumbers[i]}%`}} className={`tree-box ${classNames[i]}`}></Content>
                          )
                        })
                      }
                      </Space>
                    </Space>
                  )
                })
              }
              <Content style={{ textAlign: 'left' }} id="k-info">
                <Tag style={{ fontSize: '1.2rem', fontWeight: 'bold' }}>Slide to adjust k, current value: {this.state.k}</Tag>
              </Content>
              <Slider tooltip={{ open: false }} dots={true} defaultValue={1} min={minK} max={maxK} style={{ flex: 0.5 }} id="k-slider" step={0.01} onChange={(e) => this.setState({...this.state, k: e.valueOf()})}></Slider>
            </Space>
          }
      
      </Content>
    );
  }
};

export default TreeStructure;