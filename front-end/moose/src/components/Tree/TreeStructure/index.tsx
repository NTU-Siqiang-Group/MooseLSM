import * as React from 'react';

import { TreeOutputState } from '../../../types';

class TreeStructure extends React.Component<TreeOutputState> {
  constructor(props: TreeOutputState) {
    super(props);
  }

  render() {
    return (
      <div id="tree-structure-component">
        <div>
          <div>Tree Level: {this.props.lvl}</div>
          <div>Runs: {this.props.lvlRuns}</div>
          <div>Caps: {this.props.lvlCaps}</div>
          <div>Success: {this.props.isSucc}</div>
        </div>
      </div>
    );
  }
};

export default TreeStructure;