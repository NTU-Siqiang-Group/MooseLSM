import * as React from 'react';
import { Link } from 'react-router-dom';

import type { MenuProps } from 'antd';
import { Menu } from 'antd';


import './bar.less';

const NavBar = () => {
  return (
    <Menu mode="horizontal" style={{ display: 'flex', width: '100%', justifyContent: 'flex-start', height: '60px', lineHeight: '60px' }}>
      <Menu.Item key="homepage" icon={<div />} style={{ fontSize: '25px', lineHeight: '60px', fontWeight: 'bold' }}>
        <Link to="/" style={{ color: 'black' }}>Homepage</Link>
      </Menu.Item>

      <Menu.Item key="monitor" icon={<div />} style={{ fontSize: '25px', lineHeight: '60px', fontWeight: 'bold' }}>
        <Link to="/SystemMonitor" style={{ color: 'black' }}>Monitor</Link> 
        {/* <Link to="/SystemMonitor" style={{ color: 'black' }}>Monitor</Link>  */}
      </Menu.Item>
    
      {/* <Menu.Item key="alipay" icon={<div />}>
        Navigation Three
      </Menu.Item> */}
    </Menu>
  );
}

// const items: MenuProps['items'] = [
//   {
//     label: 'Navigation One',
//     key: 'mail',
//     icon: <div />,
//     content: <Link to="/yourPage">Navigation One</Link> // replace '/yourPage' with the path to your page
//   },
//   {
//     label: 'Navigation Two',
//     key: 'app',
//     icon: <div />,
//   },
//   {
//     label: 'Navigation Three',
//     key: 'alipay',
//     icon: <div />,
//   },
// ]

// class NavBar extends React.Component {
//   render() {
//     return (
//       <Menu mode="horizontal" items={items}/>
//     );
//   }
// }

export default NavBar;