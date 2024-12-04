import React from 'react';
import logo from './logo.svg';
import './App.css';
import "@fontsource/indie-flower"; 
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';
import SystemMonitor from './components/Charts/index';
import HomePage from './components/Tree/index';

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
        <Layout style={{ minHeight: '150vh' }}>
          <Router>
            <NavBar />
            <Routes>
              <Route path="/" element={<HomePage />} /> 
              <Route path="/SystemMonitor" element={<SystemMonitor />} />
              {/* Add other routes here */}
            </Routes>
          </Router>
        </Layout>
      </ConfigProvider>
    </div>
  );
}
export default App;
