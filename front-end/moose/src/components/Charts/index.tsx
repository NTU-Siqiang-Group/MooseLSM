import React, { useEffect, useState } from 'react';
import { Line, Column } from '@ant-design/charts';
import { Pie } from '@ant-design/plots';
import axios from 'axios';
import MooseFooter from '../Footer';
import { Typography, ConfigProvider, Table } from "antd";
// import './charts.css';

const SystemMonitor = () => {
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
    let defaultPieData: { type: string, value: number }[] = [];
    let defaultBarData: { ts: number, elapsed_time: number }[] = [];
    let defaultTableData: { stat: string, velue: number }[] = [];

    // 👆 When API is Good, switch to the single line default data definition

    const [linedata, setData] = useState(defaultData);
    const [pieData, setPieData] = useState(defaultPieData);
    const [barData, setBarData] = useState(defaultBarData);
    const [tableData, setTableData] = useState(defaultTableData);


    useEffect(() => {
        const fetchData = () => {
            axios.get('http://172.21.47.236:1080/KvServerCtrl/status')
                .then(response => {
                    console.log(response.data)

                    const getArray = (response.data.get || []).map((value: number, index: number) => {
                        return { timestamp: response.data.get_ts[index], value: value, category: 'Get' };
                    });

                    const putArray = (response.data.put || []).map((value: number, index: number) => {
                        return { timestamp: response.data.put_ts[index], value: value, category: 'Put' };
                    });

                    const rangeArray = (response.data.range || []).map((value: number, index: number) => {
                        return { timestamp: response.data.range_ts[index], value: value, category: 'Range' };
                    });

                    let workloadArray: { type: string; value: number }[] = [];

                    // console.log("workload: ", response.data.workload);

                    workloadArray = Object.entries(response.data.workload).map(([key, value]) => {
                        if (key) {
                            return { type: `${key.charAt(0).toUpperCase() + key.slice(1)}`, value: Number(value) };
                        }
                        return { type: `${key.charAt(0).toUpperCase() + key.slice(1)}`, value: Number(value) };
                    });

                    const newData = [...getArray, ...putArray, ...rangeArray];

                    const barArray = (response.data.compaction.elapsed_time || []).map((value: number, index: number) => {
                        return { ts: index + 1, elapsed_time: value };
                    });

                    const statNames = ['Bloom Negative', 'Bloom Positive', 'Bloom True Positive',
                        'Compaction Number', 'Flush Bytes', 'False Positive Rate', 'Key Number', 'Total Compaction Read Bytes', 'Total Compaction Write Bytes'];
                    console.log("response.data.stats: ", response.data.stat)
                    const tableArray = Object.entries(response.data.stat || {}).map(([key, value], index) => {
                        return { stat: statNames[index], velue: Number(value) };
                    });
                    console.log("tableArray: ", tableArray)

                    setBarData(barArray);

                    // Replace the existing data with the new data
                    setData(newData);
                    setPieData(workloadArray);
                    setTableData(tableArray);

                })
                .catch(error => {
                    console.error('There was an error!', error);
                });
        };

        fetchData(); // Call it once immediately

        const interval = setInterval(fetchData, 5000); // Then call it every 5 seconds

        // Clear interval on component unmount
        return () => clearInterval(interval);
    }, []);

    const lineconfig = {
        data: linedata,
        width: 1200,
        height: 600,
        xField: 'timestamp',
        yField: 'value',
        // colorField: '#439798',
        colorField: 'category',
        point: {
            shapeField: 'square',
            sizeField: 4,
        },
        interaction: {
            tooltip:
                true
        },
        smooth: true,
        title: {
            title: 'Real-time System Latency',
            style: { titleFontSize: 30 },
        },
        style: {
            lineWidth: 2,
        },
        legend: {
            color: { itemLabelFontSize: 20 },
            size: { colPadding: 50 }
        },
        axis: {
            x: { title: "TimeStamp", titleFontSize: 20 },
            y: { title: "Latency/μs", titleFontSize: 20 },
        },
    };

    const pieConfig = {
        width: 400,
        height: 400,
        appendPadding: 10,
        data: pieData,
        angleField: 'value',
        colorField: 'type',
        radius: 0.8,
        label: {
            // text: (d: any) => `${d.type}\n ${d.value}`,
            text: (d: any) => `${d.type.charAt(0).toUpperCase() + d.type.slice(1)}\n ${d.value}`,
            style: {
                fontWeight: 'bold',
            },
        },
        legend: {
            color: {
                itemLabelFontSize: 20,
            },
            size: { colPadding: 10 },
        },
        tooltip: (d: any) => `${d.type.charAt(0).toUpperCase() + d.type.slice(1)}\n ${d.value}`,
        title: {
            title: 'Real-time Workload Distribution',
            style: { titleFontSize: 22, align: "left" },
        },
    };

    const barConfig = {
        width: 400,
        height: 400,
        data: barData,
        xField: 'ts',
        yField: 'elapsed_time',
        title: {
            title: 'Recent Compaction Latency',
            style: { titleFontSize: 22 },
        },
        axis: {
            x: { title: "Compaction No.", titleFontSize: 20 },
            y: { title: "Latency/μs", titleFontSize: 20 },
        },
    };


    const tablecolumns = [
        {
            title: 'Stats',
            dataIndex: 'stat',
            key: 'stat',
        },
        {
            title: 'Value',
            dataIndex: 'velue',
            key: 'velue',
        },
    ]




    return (
        <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
            <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center' }}>
                <Line {...lineconfig} />
            </div>
            <div style={{ display: 'flex', flexDirection: 'row', justifyContent: 'space-around', alignItems: 'center', maxWidth: '100%', margin: '0 auto' }}>
                <div style={{ margin: '0 40px' }}>
                    <Column {...barConfig} />
                </div>
                <div style={{ margin: '0 40px' }}>
                    <Pie {...pieConfig} />
                </div>
                <div style={{ margin: '0 40px', maxHeight: "400px" }}>

                    <ConfigProvider
                        theme={{
                            components: {
                                Table: {
                                    colorBgContainer: '#e6f4ff',
                                    headerBg: '#1677ff',
                                    headerColor: '#fff',
                                    headerSortActiveBg: '#0958d9',
                                    headerSortHoverBg: '#69b1ff',
                                    bodySortBg: '#1677ff10',
                                    rowHoverBg: '#1677ff10',
                                    rowSelectedBg: '#bae0ff',
                                    rowSelectedHoverBg: '#91caff',
                                    rowExpandedBg: '#1677ff10',
                                    cellPaddingBlock: 10,
                                    cellPaddingInline: 10,
                                    cellPaddingBlockMD: 10,
                                    cellPaddingInlineMD: 10,
                                    cellPaddingBlockSM: 10,
                                    cellPaddingInlineSM: 10,
                                    borderColor: '#e6f4ff',
                                    headerBorderRadius: 0,
                                    footerBg: '#1677ff',
                                    footerColor: '#fff',
                                    cellFontSize: 16,
                                    cellFontSizeMD: 16,
                                    cellFontSizeSM: 14,
                                    headerSplitColor: '#fff',
                                    headerFilterHoverBg: 'rgba(0, 0, 0, 0.12)',
                                    filterDropdownMenuBg: '#fff',
                                    filterDropdownBg: '#fff',
                                    expandIconBg: '#e6f4ff',
                                },
                            },
                        }}
                    >
                        <Typography.Title level={2} style={{ fontSize: 22, textAlign: 'left', marginTop: '10px' }}>Real-time System Stats</Typography.Title>
                        <Table
                            dataSource={tableData}
                            columns={tablecolumns}
                            size="small"
                            // title={() => "Realtime System Stats"}
                            pagination={false}
                            style={{ maxHeight: '350px', overflow: 'auto' }}


                        />
                    </ConfigProvider>
                </div>
            </div>
            <MooseFooter />
        </div>

    );
};

export default SystemMonitor;