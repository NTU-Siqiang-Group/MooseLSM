import React, { useEffect, useState } from 'react';
import { Column } from '@ant-design/charts';
import axios from 'axios';
import MooseFooter from '../Footer';

const ChartsPage = () => {
    const defaultData = [
        { timestamp: '1997', value: 7 },
        { timestamp: '1998', value: 9 },
        { timestamp: '1999', value: 13 },
        { timestamp: '2000', value: 15 },
        { timestamp: '2001', value: 16 },
        { timestamp: '2002', value: 18 },
        { timestamp: '2003', value: 20 },
        { timestamp: '2004', value: 22 },
        { timestamp: '2005', value: 24 },
        { timestamp: '2006', value: 26 },
        { timestamp: '2007', value: 28 },
        { timestamp: '2008', value: 30 },
        { timestamp: '2009', value: 32 },
        { timestamp: '2010', value: 34 },
        { timestamp: '2011', value: 36 },
        { timestamp: '2012', value: 38 },
        { timestamp: '2013', value: 40 },
    ];

    const [data, setData] = useState(defaultData);

    useEffect(() => {
        const interval = setInterval(() => {
            axios.get('http://127.0.0.1:5000/api/monitor')
                .then(response => {
                    setData(response.data);
                })
                .catch(error => {
                    console.error('There was an error!', error);
                });
        }, 5000); // Call API every 5 seconds

        // Clear interval on component unmount
        return () => clearInterval(interval);
    }, []);

    const config = {
        data,
        width: 1200,
        height: 400,
        xField: 'timestamp',
        yField: 'value',
        colorField: '#c1decb',
    };

    return (
        <div>
        <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', height: '100vh', paddingTop: '50vh' }}>
          <div style={{ paddingBottom: '50vh' }}>
            <Column {...config} />
          </div>
        </div>
        <MooseFooter />
      </div>

    );
};

export default ChartsPage;