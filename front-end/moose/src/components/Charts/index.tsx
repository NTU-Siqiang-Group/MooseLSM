import React, { useEffect, useState } from 'react';
import { Line } from '@ant-design/charts';
import axios from 'axios';
import MooseFooter from '../Footer';

const ChartsPage = () => {
    // const defaultData = [
    //     { timestamp: '1997', value: 7, category: 'Line 1' },
    //     { timestamp: '1998', value: 9, category: 'Line 1' },
    //     { timestamp: '1999', value: 13, category: 'Line 1' },
    //     { timestamp: '2000', value: 15, category: 'Line 1' },
    //     { timestamp: '2001', value: 16, category: 'Line 1' },
    //     { timestamp: '2002', value: 18, category: 'Line 1' },
    //     { timestamp: '2003', value: 20, category: 'Line 1' },
    //     { timestamp: '2004', value: 22, category: 'Line 1' },
    //     { timestamp: '2005', value: 24, category: 'Line 1' },
    //     { timestamp: '2006', value: 26, category: 'Line 1' },
    //     { timestamp: '2007', value: 28, category: 'Line 1' },
    //     { timestamp: '2008', value: 30, category: 'Line 1' },
    //     { timestamp: '2009', value: 32, category: 'Line 1' },
    //     { timestamp: '2010', value: 34, category: 'Line 1' },
    //     { timestamp: '2011', value: 36, category: 'Line 1' },
    //     { timestamp: '2012', value: 38, category: 'Line 1' },
    //     { timestamp: '2013', value: 40, category: 'Line 1' },
    //     { timestamp: '1997', value: 8, category: 'Line 2' },
    //     { timestamp: '1998', value: 15, category: 'Line 2' },
    //     { timestamp: '1999', value: 23, category: 'Line 2' },
    //     { timestamp: '2000', value: 35, category: 'Line 2' },
    //     { timestamp: '2001', value: 40, category: 'Line 2' },
    //     { timestamp: '2002', value: 45, category: 'Line 2' },
    //     { timestamp: '2003', value: 50, category: 'Line 2' },
    //     { timestamp: '2004', value: 55, category: 'Line 2' },
    //     { timestamp: '2005', value: 60, category: 'Line 2' },
    //     { timestamp: '2006', value: 65, category: 'Line 2' },
    //     { timestamp: '2007', value: 70, category: 'Line 2' },
    //     { timestamp: '2008', value: 75, category: 'Line 2' },
    //     { timestamp: '2009', value: 80, category: 'Line 2' },
    //     { timestamp: '2010', value: 85, category: 'Line 2' },
    //     { timestamp: '2011', value: 90, category: 'Line 2' },
    //     { timestamp: '2012', value: 60, category: 'Line 2' },
    //     { timestamp: '2013', value: 50, category: 'Line 2' },
    // ];
    let defaultData: { timestamp: string, value: number, category: string }[] = [];

    const [data, setData] = useState(defaultData);

    useEffect(() => {
        const interval = setInterval(() => {
            axios.get('http://127.0.0.1:8080/KvServerCtrl/status')
                .then(response => {
                    const newData = [
                        {
                            timestamp: response.data.get_ts.toString(),
                            value: response.data.get,
                            category: 'Line 1',
                        },
                        {
                            timestamp: response.data.put_ts.toString(),
                            value: response.data.put,
                            category: 'Line 2',
                        },
                    ];

                    // Append the new data to the existing data
                    setData(prevData => [...prevData, ...newData]);
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
        // colorField: '#439798',
        colorField: 'category',

        point: {
            shapeField: 'square',
            sizeField: 4,
        },
        interaction: {
            tooltip: {
                marker: false,
            },
        },
        style: {
            lineWidth: 2,
        },
        smooth: true,
    };



    return (
        <div>
            <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', height: '100vh', paddingTop: '50vh' }}>
                <div style={{ paddingBottom: '50vh' }}>
                    <Line {...config} />
                </div>
            </div>
            <MooseFooter />
        </div>

    );
};

export default ChartsPage;