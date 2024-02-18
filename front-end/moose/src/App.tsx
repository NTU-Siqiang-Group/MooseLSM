import React from 'react';
import logo from './logo.svg';
import './App.css';

import { Layout, ConfigProvider } from 'antd';

import Tree from './components/Tree';
import NavBar from './components/NavBar';

const primaryColor = '#009999';
const bgColor = '#f6ffed';

const { Content, Header } = Layout;

function App() {
  return (
    <div className="App">
    <ConfigProvider
      theme={{
        token: {
          colorPrimary: primaryColor,
          borderRadius: 2,
          colorBgContainer: bgColor,
          fontSize: 15,
          fontFamily: '"Helvetica Neue",Helvetica,Arial,sans-serif',
        },
        components: {
          Menu: {
            itemBg: primaryColor,
          },
        }
      }}
    > 
    <Layout>
      <NavBar />
      <Tree />
    </Layout>
    </ConfigProvider>
    </div>
  );
}

export default App;
