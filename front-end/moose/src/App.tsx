import React from 'react';
import logo from './logo.svg';
import './App.css';
import "@fontsource/indie-flower"; 

import { Layout, ConfigProvider, Space } from 'antd';

import Tree from './components/Tree';
import NavBar from './components/NavBar';
import MooseFooter from './components/Footer';

const primaryColor = '#009999';
const bgColor = '#f6ffed';

const { Content } = Layout;

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
    <Layout style={{ minHeight: '100vh' }}>
      <NavBar />
      <Content style={{fontFamily: "'Indie Flower', cursive", fontSize: "3rem", backgroundColor: '#badfca'}}>
        <Space id="title" direction='vertical' style={{ display: "flex" }}>
          <Content>Structural Designs <u>M</u>eet <u>O</u>ptimality:</Content>
          <Content>Exploring <u>O</u>ptimized LSM-tree <u>S</u>tructures in A Colossal Configuration Spac<u>e</u></Content>
          <Content id="moose-img"> 
            <img src='supermoose.png'/>
          </Content>
        </Space>
      </Content>
      <Tree />
      <MooseFooter />
    </Layout>
    </ConfigProvider>
    </div>
  );
}

export default App;
