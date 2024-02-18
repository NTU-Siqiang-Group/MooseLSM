import * as React from 'react';

import type { MenuProps } from 'antd';
import { Menu } from 'antd';

import './bar.less';

const items: MenuProps['items'] = [
  {
    label: 'Navigation One',
    key: 'mail',
    icon: <div />,
  },
  {
    label: 'Navigation Two',
    key: 'app',
    icon: <div />,
  },
  {
    label: 'Navigation Three',
    key: 'alipay',
    icon: <div />,
  },
]

class NavBar extends React.Component {
  render() {
    return (
      <Menu mode="horizontal" items={items}/>
    );
  }
}

export default NavBar;