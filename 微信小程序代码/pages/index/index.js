Page({
  data: {
    Temp:0,
    Hum:0
  },
    
  config: {
      authorization: "version=2022-05-01&res=products%2FSQ8gfZ73EX&et=1808203155&method=sha1&sign=Jm8kOvSKGafOPYd%2B94e94VWpMn4%3D", // 鉴权信息
      product_id: "SQ8gfZ73EX", // 产品ID
      device_name: "Test1", // 设备名称
      getinfo_url: 'https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=SQ8gfZ73EX&device_name=Test1', //设备属性最新数据查询地址
      
      setinfo_url:'https://iot-api.heclouds.com/thingmodel/set-device-property'//设置设备属性
    },
  Onenet_GetInfo()
  {
    wx.request({
      url: this.config.getinfo_url,
      header:{
        'authorization':this.config.authorization
      },
      method:"GET",
      success: (e) =>
      {
          console.log(e),
          this.setData({
          Temp:e.data.data[3].value,
          Hum:e.data.data[1].value
        })
      }
    })
  }
  
  ,
  Onenet_SetAlarmInfo(event)
  {
    const is_checked = event.detail.value; // 获取开关状态
    wx.showToast({
      title: '操作成功', // 提示的文字内容
      icon: 'success', // 图标类型，使用成功图标
      duration: 1000 // 提示框自动隐藏的时间，单位是毫秒
    }), 
    wx.request({
      url: this.config.setinfo_url,
      header:{
        'authorization':this.config.authorization
      },
      method:"POST",
      data: {
        "product_id": this.config.product_id,
        "device_name": this.config.device_name,
        "params": {
          "Alarm": is_checked
        }     
      },            
    })
  },    
  Onenet_SetLedInfo(event)
  {
    const is_checked = event.detail.value; // 获取开关状态
    wx.showToast({
      title: '操作成功', // 提示的文字内容
      icon: 'success', // 图标类型，使用成功图标
      duration: 1000 // 提示框自动隐藏的时间，单位是毫秒
    }), 
    wx.request({
      url: this.config.setinfo_url,
      header:{
        'authorization':this.config.authorization
      },
      method:"POST",
      data: {
        "product_id": this.config.product_id,
        "device_name": this.config.device_name,
        "params": {
          "Led": is_checked
        }     
      },            
    })
  },   
  onLoad(){
    setInterval(this.Onenet_GetInfo,5000)
  }
})
