import * as React from 'react';

import { Result, Space, Slider, Tag } from 'antd';
import { TreeOutputState } from '../../../types';
import { Content } from 'antd/es/layout/layout';
import './structure.less';
import EChartsReact from 'echarts-for-react';

interface TreeStructureState {
  k: number;
}

interface TreeStructureProps extends TreeOutputState {
  kvSize: number;
  blockSize: number;
}

class TreeStructure extends React.Component<TreeStructureProps, TreeStructureState> {
  constructor(props: TreeStructureProps) {
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
    const maxWidth = 100;
    const minWidth = 50;
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
    const rrCost: any[] = [];
    const wCost: any[] = [];
    let posY = -1;
    for (let k = minK; k < maxK; k += 0.01) {
      const rns: number[] = this.props.nis.map((ni, i): number => {
        return Math.round(Math.min(Math.max(1, ni * k), this.props.ris[i]));
      });
      const rr = rns.reduce((acc, cur) => acc + cur, 0);
      const w = Number((
        rns.map((ni, i): number => Number((this.props.ris[i] / ni).toFixed(3)))
        .reduce((acc, cur) => acc + cur, 0) * this.props.kvSize / this.props.blockSize
      ).toFixed(3));
      if (rrCost.length === 0) {
        rrCost.push(rr);
        wCost.push(w);
      } else if (rr !== rrCost[rrCost.length - 1]) {
        rrCost.push(rr);
        wCost.push(w);
      }
      if (this.state.k.toFixed(2) === k.toFixed(2)) {
        posY = w;
      }
    }
    const option = {
      title: {
        text: 'Pareto Curve: Range Lookup vs. Write Cost',
      },
      tooltip: {
        trigger: 'axis',
      },
     
      xAxis: {
        name: 'Range Lookup Cost (IO)',
        nameLocation: 'middle',
        nameGap: 25,
        type: 'category',
        data: rrCost,
      },
      yAxis: {
        name: 'Write Cost (IO)',
        nameLocation: 'middle',
        nameGap: 25,
        type: 'value',
        data: wCost
      },
      series: [
        {
          data: wCost,
          type: 'line',
          smooth: true,
          name: 'MooseCurve'
        }
      ],
      visualMap: {
        pieces: [
          {
            gte: posY,
            lte: posY,
            label: 'Current tradeoff',
          },
        ],
        outOfRange: {
          color: '#009999',
          // symbol: 'circle',
          // symbolSize: 5,
        },
        inRange: {
          color: '#FD0100',
          symbol: 'cicle',
          symbolSize: 6,
        },
        show: true,
      },
      color: ['#009999'],
    };
    return (
      <Space style={{ paddingTop: '1rem', paddingBottom: '1rem', backgroundColor: 'white', width: '80%' }} align='center'>
          {isSucc === false ?
            errMsg.length === 0 ?
              <Content style={{ fontSize: '20px' }}>Click Compute and Generate a Preview of your LSM-tree!</Content>
              :
              <Result
                status="warning"
                title={errMsg}
              >
              </Result>
             :
            <Space direction='vertical' style={{ display: 'flex', width: '100%' }} align='center'>
              <Space direction='horizontal' style={{ display: 'flex', alignItems: 'start' }} id="tree-display-wrapper">
                <Space direction='vertical' style={{display: "flex", flex: 1 }} align='center' id="tree-wrapper">
                  <Content style={{ textAlign: 'center' }} id="k-info">
                    <Tag style={{ fontSize: '1.2rem', fontWeight: 'bold' }}>Slide to adjust k, current value: {this.state.k}</Tag>
                  </Content>
                {
                  this.props.ris.map((_, i) => {
                    return (
                      <Space style={{ display: "flex", width: `${lvlWidths[i]}%` }} direction='vertical' align='center'>
                        <Content>Cap.: {this.props.lvlCaps[i]}, #runs: {runNumbers[i]}</Content>
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
                </Space>
                <EChartsReact option={option} style={{ flex: 1 }}></EChartsReact>
              </Space>
              <Slider tooltip={{ open: true, formatter(value) { return `k: ${value}` }, }} dots={true} defaultValue={1} min={minK} max={maxK} step={0.01} onChange={(e) => this.setState({...this.state, k: e.valueOf()})}></Slider>
            </Space>
          }
      </Space>
    );
  }
};

export default TreeStructure;