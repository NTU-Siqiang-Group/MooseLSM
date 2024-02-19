import * as React from 'react';

import { Result, Space } from 'antd';
import { TreeOutputState } from '../../../types';
import { Content } from 'antd/es/layout/layout';

import './structure.less';

interface TreeStructureState {
  runNumbers: number[];
  k: number;
}

class TreeStructure extends React.Component<TreeOutputState, TreeStructureState> {
  constructor(props: TreeOutputState) {
    super(props);
    this.state = {
      runNumbers: this.props.nis,
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
    return (
      <Content style={{ marginTop: '1rem' }}>
          {this.props.isSucc === false ?
            <Result 
              status="warning"
              title={this.props.errMsg}
            >
            </Result>
             :
            <Space direction='vertical' style={{display: "flex"}} align='center' id="tree-wrapper">
              {
                this.props.ris.map((_, i) => {
                  return (
                    <Space style={{ display: "flex", width: `${lvlWidths[i]}%` }} direction='vertical' align='center'>
                      <Content>Capacity: {this.props.lvlCaps[i]}</Content>
                      <Space direction='horizontal' style={{ display: "flex" }} className="lvl-wrapper">
                      {
                        Array.from({length: this.state.runNumbers[i]}, (v, j) => j).map(() => {
                          return (
                            <Content style={{ width: `${lvlWidths[i] / this.state.runNumbers[i]}%`}} className={`tree-box ${classNames[i]}`}></Content>
                          )
                        })
                      }
                      </Space>
                    </Space>
                  )
                })
              }
            </Space>
          }
      </Content>
    );
  }
};

export default TreeStructure;