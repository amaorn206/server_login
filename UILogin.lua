local Lplus = require "Lplus"
local ECPanelBase = require "GUI.ECPanelBase"
local UserData = require "Data.UserData"
local LoginData = require("Game.Login.data.LoginData").Instance()
local UILogin = Lplus.Extend(ECPanelBase, "UILogin");
local ECGame = Lplus.ForwardDeclare("ECGame")
local MsgBox = require "Game.Common.ui.MsgBox"
local WebLoginMan = require "Game.Login.WebLoginMan" 
local ConfigMgr = require "Main.ConfigMgr".Instance()
local def =UILogin.define
local ECSoundMan = require "Sound.ECSoundMan"
local ECPanelNotifyMsg = require "GUI.ECPanelNotifyMsg"
local m_Instance = nil
local cfgStr ="fastlogin__"
--？？
def.field("userdata").m_loginObj =nil
def.field("userdata").m_inputAccount = nil
def.field("userdata").m_inputPassword = nil
def.field("userdata").m_fastToggle = nil
def.field("userdata").m_openNetToggle = nil
def.field("userdata").m_serverRoot = nil
def.field("userdata").m_serverText = nil
def.field("userdata").m_serverState = nil
def.field("userdata").m_loginBg =nil
def.field("userdata").m_login_btn_pc = nil
def.field("userdata").m_login_btn_ks = nil

def.field("userdata").btn_notice = nil
def.field("userdata").btn_user = nil
def.field("userdata").btn_switch = nil

def.field("userdata").maskGo = nil

local supportOwnedRole = true

def.static("=>",UILogin).Instance = function()
	if(m_Instance == nil) then
	     m_Instance = UILogin()
	end
	return m_Instance
end
def.override().DoCreate =function(self)


    local res_path_id = 19000010
    if _G.cur_quality_level <= 1 then
        res_path_id = 19000020
    end

    local go = GameObject.Find("UGUIRoot")
    go:SetActive(false)
    GameUtil.AddGlobalTimer(0.5,true,function (  ) --第一帧保证UI隐藏
        ECGame.Instance():SetQualityLevel(ECGame.Instance():GetDefaultQuality(),true)
        GameUtil.AddGlobalTimer(0.1,true,function (  )--第二帧保证分辨率正常修改
            --删除预加载ui
            GameUtil.DestroyGameUpdateBackgroundDialog()
            go:SetActive(true)
            GameUtil.AsyncLoad(GetResPathById(res_path_id), function(obj)
                if obj and not obj.isnil then
                    self.m_loginBg = Object.Instantiate(obj,"GameObject")
                else
                    warn("没有找到资源 id ==  ",19000010)
                end

                self:CreateUGUIPanel(GetResPathById(10000008),3)
            end)
        end)
    end)


    GameUtil.SetNeedUpdateWirelessNetwork( false )
    warn("_G.cur_quality_level =", _G.cur_quality_level )
end

def.override().OnCreate =function ( self )

    local panel = self.m_panel:FindDirect("layout"):GetComponent("RectTransform")
    Lplus.ForwardDeclare("ECGUIMan").Instance():NotchAdaptation(panel,1)

    -- ECSoundMan.Instance():PlayBackgroundMusicByIDLoop("bg_musicloop",91000004)
    ECSoundMan.Instance():PlayBackgroundMusicByID("bg_music",91000004)
    -- body
    self.m_loginObj = self.m_panel:FindDirect("login")
    local go = self.m_loginObj:FindDirect("input_account")
    self.m_inputAccount= go:GetComponent("InputField")
    go:unbind()
    local go1 = self.m_loginObj:FindDirect("input_pass")
    self.m_inputPassword= go1:GetComponent("InputField")
    go1:unbind()

    local go2 = self.m_loginObj:FindDirect("toggle_fastenter")
    self.m_fastToggle =go2:GetComponent("Toggle")
    go2:unbind()

    local go3 = self.m_loginObj:FindDirect("toggle_opennet")
    self.m_openNetToggle = go3:GetComponent("Toggle")
    go3:unbind()
    --self.m_fastToggle.isOn = (value==1)
    --self.m_openNetToggle.isOn = (value==2)
    local loginModule = gmodule.moduleMgr:GetModule(ModuleId.Login)
    self.m_inputAccount.text = GetLocalSave("account","")

    self.m_inputPassword.text = loginModule.m_password
    --self.m_enterGameObj = self.m_panel:FindChild("entergame")
    self.m_serverRoot = self.m_loginObj:FindDirect("btn_selectserver")
    local go4 = self.m_loginObj:FindDirect("btn_selectserver/server_text")
    self.m_serverText = go4:GetComponent("Text")
    go4:unbind()

    local go5 = self.m_loginObj:FindDirect("btn_selectserver/icon_state")
    self.m_serverState = go5:GetComponent("Image")
    go5:unbind()

    local go6 = self.m_panel:FindDirect("layout/btn_notice")
    self.btn_notice = go6:GetComponent("Button")
    go6:unbind()

    self.btn_user = self.m_panel:FindDirect("layout/btn_user")
    self.btn_user:SetActive(false)

    self.btn_switch = self.m_panel:FindDirect("layout/btn_switch")
    self.btn_switch:SetActive(false)

    self.maskGo = self.m_panel:FindDirect("mask")
    self.maskGo:SetActive(false)
    --local chooseText = self.m_enterGameObj:FindChild("choose_text"):GetComponent("Text")
    --chooseText.text =  UIUtils.GetText(11105)
    --没有相关逻辑暂时写死
    if LoginData.m_LoginServerInfo then
        self.m_serverText.text =  LoginData.m_LoginServerInfo.name
        UIUtils.SetOverrideSpriteFromAtlas(self.m_serverState,COMMON_ATLAS.CREATE_ROLE_V2,LoginData:GetStateImage(tonumber(LoginData.m_LoginServerInfo.state)),false)
    else
        self.m_serverRoot:SetActive(false)
    end

    self.m_login_btn_pc = self.m_panel:FindDirect("login/button_login")
    self.m_login_btn_ks = self.m_panel:FindDirect("login/button_login_ks")
    

    --这是强制输出日志信息
    logWarn("load login finish time = ", Time.realtimeSinceStartup )
    

    -- local ECGUIMan = require "GUI.ECGUIMan"
    -- ECGUIMan.Instance():PreLoadRes()

    --add by ares，自动登录测试
    if IsAutoTest then
        GAutomator.Instance:RegisterCallBack("AutoSetIp")
    end

    local currentVersion = GameUtil.GetLocalResourceVersion()

    local verson_str = "v " .. currentVersion[1] .. "." .. currentVersion[2] .. "." .. currentVersion[3]

    local v = currentVersion[3]
    local svn_verson = _G.patch2version[v]

    for k, v in pairs(patch2version) do
        warn("patch =", k, v)
    end

    warn("version =", currentVersion[3],svn_verson )
    if svn_verson then
        verson_str = verson_str .. "_" .. svn_verson
    end

    local go = self.m_panel:FindChild("text_version")
    go:GetComponent("Text").text = verson_str
    go:unbind()

    self:ResetLoginState()
end

def.method().ResetLoginState = function ( self )
    local loginModule = gmodule.moduleMgr:GetModule(ModuleId.Login)
    loginModule:ResetLoginState()
    self:ShowStage(1)
    self:ShowSDKNotify(true)
end

def.override().AfterCreate = function(self)
    --如果是非SDK的，直接获取服务器信息
    if not YWSDKProxy.GetCurSDK():SDKEnable() then
        self:CheckServerList(function ( success )
            if success then
                local acct = self.m_inputAccount.text
                local match_res = string.match(acct,"[%w_]+")
                print("match_res :",match_res)
                if not match_res or match_res:len() ~= acct:len() then
                    acct = ""
                end
                acct = acct:len() == 0 and "123" or acct
                self.m_inputAccount.text = acct
                WebLoginMan.Instance():RequestTFLogin(acct,function(is_gm,tf_id)
                    self:updateServerUI(tf_id)
                end,function ( code,retdata )
                    print("login error :",retdata)
                end,WebLoginMan.ERequestType.ServerList)
            end
        end)
    end
end

def.method("function").CheckServerList = function ( self,callback )
    if LoginData.m_serverList == nil then
        LoginData:DownloadServerList(function()
            if LoginData.m_serverList and #LoginData.m_serverList > 0 then
                if callback then callback(true) end
            else
                LoginData:ShowErrorTips("获取服务器列表失败!")
                warn("服务器列表ip:",GameUtil.GetWebUrl())
                if callback then callback(false) end
            end
        end)
    else
        if callback then callback(true) end
    end
end

def.method("number").updateServerUI = function ( self,tf_id )

    local refreshUI = function (  )
        if YWSDKProxy.GetCurSDK():SDKEnable() then
            self.m_login_btn_ks:SetActive(true)
        end
        if not LoginData.m_LoginServerInfo then
            self.m_serverRoot:SetActive(false)
            return
        end
        self.m_serverRoot:SetActive(true)
        self.m_serverText.text = LoginData.m_LoginServerInfo.name
        LoginData:SetLoginServerInfo(tonumber(LoginData.m_LoginServerInfo.no))
        UIUtils.SetOverrideSpriteFromAtlas(self.m_serverState,COMMON_ATLAS.CREATE_ROLE_V2,
            LoginData:GetStateImage(tonumber(LoginData.m_LoginServerInfo.state)),false)
    end
    if supportOwnedRole then
        if not YWSDKProxy.GetCurSDK():SDKEnable() then
            local acct = self.m_inputAccount.text
            if acct:len() > 0 then
                LoginData:RequireRoleList(tf_id,function()
                    refreshUI()
                end)
            else
                refreshUI()
            end
        else
            LoginData:RequireRoleList(tf_id,function()
                refreshUI()
            end)
        end

    else
        refreshUI()
    end
    -- if LoginData.m_LoginServerInfo then


    -- end
end

def.method("boolean").ShowPCUI = function ( self,show)
    self.m_inputAccount.gameObject:SetActive(show)
    self.m_login_btn_pc:SetActive(show)
end

def.method("number").DoKSLogin = function ( self, type)
    if type == WebLoginMan.ERequestType.Login and not LoginData:LoginServerOpen()  then
        return
    end
    --请求web服务器
    local channel = SystemInfoBridge.GetChannelId()
    local openId = KSSDKProxy.Instance().openId

    print("DoKSLogin :",channel,openId)
    WebLoginMan.Instance():RequestKaiserLogin(channel,openId,function (tf_token,tf_id)
        if type == WebLoginMan.ERequestType.ServerList then
            self:OnSDKResponse(tf_id)
        else
            self:OnWebResponse(tf_token)
        end
    end,function ( code,retdata )
        if type == WebLoginMan.ERequestType.Login then
            --失败后需要重新弹出登录界面
            self.m_login_btn_ks:SetActive(true)
            if code == -1 then
                MsgBox.ShowMsgBox(nil,"网络异常!",nil,MsgBox.MsgBoxType.MBBT_OK,nil,nil,nil,Priority.disconnect,function(self)
                    self:SetOkText( UIUtils.GetText(1))
                end)
            else
                MsgBox.ShowMsgBox(nil,"服务器异常!错误码：acc_" .. code,nil,MsgBox.MsgBoxType.MBBT_OK,nil,nil,nil,Priority.disconnect,function(self)
                    self:SetOkText( UIUtils.GetText(1))
                end)
            end
            
        end
    end,type)
end

def.method("number").DOMiLogin = function ( self ,type)
    if not LoginData:LoginServerOpen() and type == WebLoginMan.ERequestType.Login then
        return
    end
    local uid = XiaoMiSDKProxy.Instance().uid
    local session = XiaoMiSDKProxy.Instance().sessionId
    WebLoginMan.Instance():RequestMILogin(uid,session,function (tf_token,tf_id)
        if type == WebLoginMan.ERequestType.ServerList then
            self:OnSDKResponse(tf_id)
        else
            self:OnWebResponse(tf_token)
        end
    end,function ( code,retdata )
        if type == WebLoginMan.ERequestType.Login then
            --失败后需要重新弹出登录界面
            self.m_login_btn_ks:SetActive(true)
            MsgBox.ShowMsgBox(nil,"登录失败!错误码：mi_"..code,nil,MsgBox.MsgBoxType.MBBT_OK,nil,nil,nil,Priority.disconnect,function(self)
                self:SetOkText( UIUtils.GetText(1))
            end)
        end
    end,type)
end

def.method().ShowSDKLoginDialog = function ( self )
    self.m_login_btn_ks:SetActive(false)
    local uid = YWSDKProxy.GetCurSDK().uid
    local loginCallback = function ( obj )
        print("ks sdk login callback :",obj.code,obj.uid,obj.openId)
        if tostring(obj.code) == "0" or tostring(obj.code) == "10000" then
            if GameUtil.GetProxySDK() == 1 then
                self:ShowSDKNotify(true)

                -- self.m_serverText.text = LoginData.m_LoginServerInfo.name
                -- LoginData:SetLoginServerInfo(tonumber(LoginData.m_LoginServerInfo.no))
                -- UIUtils.SetOverrideSpriteFromAtlas(self.m_serverState,COMMON_ATLAS.CREATE_ROLE_V2,LoginData:GetStateImage(tonumber(LoginData.m_LoginServerInfo.state)),false)

                self:DoKSLogin(WebLoginMan.ERequestType.ServerList)
                --凯撒的特殊需求
                local show_center = KSSDKProxy.Instance():IsKaiserChannel()
                local isSupportLogout = KSSDKProxy.Instance():IsSupportLogout()
                self.btn_user:SetActive(show_center)
                self.btn_switch:SetActive(isSupportLogout)

                --self.m_login_btn_ks:SetActive(true)
            end
            if GameUtil.GetProxySDK() == 2 then
                self:DOMiLogin(WebLoginMan.ERequestType.ServerList)
            end
            self.m_login_btn_ks:SetActive(true)
        else
            self:ShowSDKNotify(false)
            self.m_login_btn_ks:SetActive(false)
            --UITextTipsMan.Instance():Popup("登录失败,错误码：ks_"..obj.code)
            YWSDKProxy.GetCurSDK():Login()
        end
        
    end
    --openid如果是空,弹出登录界面
    if uid:len() == 0 then
        YWSDKProxy.GetCurSDK():RegisterLoginCallback(loginCallback)
        YWSDKProxy.GetCurSDK():Login()
        print("KSSDKProxy login")
    else
        local show_center = KSSDKProxy.Instance():IsKaiserChannel()
        local isSupportLogout = KSSDKProxy.Instance():IsSupportLogout()
        self.btn_user:SetActive(show_center)
        self.btn_switch:SetActive(isSupportLogout)
        self.m_login_btn_ks:SetActive(true)
    end
end

def.method("string").OnWebResponse = function ( self,tf_token)

    -- local mem = tonumber(SysInfo.GetTotalMemory())

    -- if mem <= 2048 and Application.platform == RuntimePlatform.Android then

    --     MsgBox.ShowMsgBoxEx(nil,"对不起，您的机型不在本次测试范围内!", nil, MsgBox.MsgBoxType.MBBT_OK,
    --         function (sender, ret )
    --             Application.Quit()
    --         end,
    --         nil, nil, Priority.disconnect,
    --         function (msgbox)
    --             msgbox.m_depthLayer = GUIDEPTH.TOPMOST
    --             msgbox:BringTop()
    --         end)
    --     return
    -- end

    UIWaiting.Instance():Popup()
    local loginModule = gmodule.moduleMgr:GetModule(ModuleId.Login)
    loginModule:OnLogin(tf_token,false,false)
end

--sdk连接成功后请求列表服务器，点击换区按钮后重新请求历史角色信息
def.method("number").OnSDKResponse = function(self,tf_id)
        --self.m_login_btn_ks:SetActive(false)
    self:updateServerUI(tf_id)
end

--显示SDK连接公告，登录成功后销毁
def.method("boolean").ShowSDKNotify = function(self,show)
    if show then
        self.maskGo:SetActive(true)
        ECPanelNotifyMsg.Instance():DownloadNotice(ECPanelNotifyMsg.ENotifyType.SDKNotify,function ( res )
            print("ShowSDKNotify :",res)
            self.btn_notice.gameObject:SetActive(res)
            self.maskGo:SetActive(false)
        end)
    else
        if ECPanelNotifyMsg.Instance():IsShow() then
            ECPanelNotifyMsg.Instance():DestroyPanel()
        end
    end
end

function _G.AutoSetIp(ip)
    ECGame.Instance():DebugString(string.format("ip %s",ip))
end

def.method().ShowLogin = function(self)
    if YWSDKProxy.GetCurSDK():SDKEnable() then
        self:ShowPCUI(false)
        --self.m_login_btn_ks:SetActive(true)
        --弹凯撒的界面
        print("ShowLogin :")
        self:CheckServerList(function ( success ) --先检查一遍服务器列表
            --成功失败都弹登录界面
            self:ShowSDKLoginDialog()
        end)
    else
        self.m_login_btn_ks:SetActive(false)
    end
end

def.method("boolean").ShowServerList = function(self,show)
    if show then
        if LoginData.m_LoginServerInfo then
            local UIServerList = require "Game.Login.ui.UIServerList"
            local callback = function(is_gm)
                LoginData:RefreshServerState(is_gm==1)
                UIServerList.Instance():PopUp()
                UIServerList.Instance():SetSelectCallback(function ( data )
                    self.m_serverText.text = data.name
                    LoginData:SetLoginServerInfo(tonumber(data.no))
                    UIUtils.SetOverrideSpriteFromAtlas(self.m_serverState,COMMON_ATLAS.CREATE_ROLE_V2,LoginData:GetStateImage(tonumber(data.state)),false)
                end)
            end
            if self.m_login_btn_ks.activeSelf then
                if GameUtil.GetProxySDK() == 1 then
                    --请求web服务器
                    local channel = SystemInfoBridge.GetChannelId()
                    local openId = KSSDKProxy.Instance().openId
                    WebLoginMan.Instance():RequestKaiserLogin(channel,openId,function (is_gm,tf_id)
                        if supportOwnedRole then
                            LoginData:RequireRoleList(tf_id,function()
                                callback(is_gm)
                            end)
                        else
                            callback(is_gm)
                        end
                        
                    end,function ( code,retdata )
                        callback(0)
                        print("login error :",retdata)
                    end,WebLoginMan.ERequestType.ServerList)
                elseif GameUtil.GetProxySDK() == 2 then
                    local uid = XiaoMiSDKProxy.Instance().uid
                    local session = XiaoMiSDKProxy.Instance().sessionId
                    WebLoginMan.Instance():RequestMILogin(uid,session,function (is_gm,tf_id)
                        if supportOwnedRole then
                            LoginData:RequireRoleList(tf_id,function()
                                callback(is_gm)
                            end)
                        else
                            callback(is_gm)
                        end
                    end,function ( code,retdata )
                        callback(0)
                        print("login error :",retdata)
                    end,WebLoginMan.ERequestType.ServerList)
                end
            else
                --切换账号后又需要请求该账号的历史角色信息
                local acct = self.m_inputAccount.text
                print("acct ====:",acct)
                if acct:len() == 0 then
                    LoginData:ClearRoleList()
                    LoginData:RefreshServerList()
                    callback(is_gm)
                else
                    WebLoginMan.Instance():RequestTFLogin(acct,function(is_gm,tf_id)
                        LoginData:RequireRoleList(tf_id,function()
                            callback(is_gm)
                        end)
                    end,function ( code,retdata )
                        callback(0)
                       print("login error :",retdata)
                    end,WebLoginMan.ERequestType.ServerList)
                end
                
            end
        else
            warn("no server info!")
        end
    end
end

def.method("string").onClick = function(self,name)
    -- if ECPanelNotifyMsg.Instance():IsShow() then
    --     self.maskGo:SetActive(false)
    --     self:ShowSDKNotify(false)
    --     return
    -- end
    if name =="button_login" then
        -- if IsOffline then
        --     ECGame.Instance():DebugString("offline")
        --     return
        -- end
        -- body
        if not LoginData:LoginServerOpen() then
            return
        end
        local account = self.m_inputAccount:get_text()
        if(account =="") then
            MsgBox.ShowMsgBox(nil,"请输入账号",nil,MsgBox.MsgBoxType.MBBT_OK,nil,nil,nil,Priority.disconnect,function(self)
				self:SetOkText( UIUtils.GetText(1))
			end)
            return
        end
        local match_res = string.match(account,"[%w_]+")
        print("match_res :",match_res)
        if not match_res or match_res:len() ~= account:len() then
            MsgBox.ShowMsgBox(nil,"账号格式为字母数字和下划线组成！",nil,MsgBox.MsgBoxType.MBBT_OK,nil,nil,nil,Priority.disconnect,function(self)
                self:SetOkText( UIUtils.GetText(1))
            end)
            return
        end
        value = 0 
        if self.m_fastToggle.isOn then
            value =1
        elseif self.m_openNetToggle.isOn then 
            value =2
        end 
        LocalSave(cfgStr,value)
        LocalSave("account",self.m_inputAccount.text)
        SaveToFile()

        -- WebLoginMan.Instance():RequestTFLogin(self.m_inputAccount.text,function ( tf_token ,tf_id)
        --     self:OnWebResponse(tf_token)
        -- end,function ( code,retdata )
        --     MsgBox.ShowMsgBox(nil,"登录失败!错误码："..code,nil,MsgBox.MsgBoxType.MBBT_OK,nil,nil,nil,Priority.disconnect,function(self)
        --         self:SetOkText( UIUtils.GetText(1))
        --     end)
        --     --UITextTipsMan.Instance():Popup("登录失败," .. retdata)
        -- end,WebLoginMan.ERequestType.Login)

        UIWaiting.Instance():Popup()
        local loginModule = gmodule.moduleMgr:GetModule(ModuleId.Login)
        loginModule:OnLogin("",false,false)

    elseif name =="btn_begin" then
        --次数暂时改为直接进入创选角待后面逻辑补充
        --Event.DispatchEvent(ModuleId.Login,gmodule.notifyId.Login.SERVER_SUCCESS,nil)
    elseif name == "button_login_ks" then
        if GameUtil.GetProxySDK() == 1 then
            self:DoKSLogin(WebLoginMan.ERequestType.Login)
        elseif GameUtil.GetProxySDK() == 2 then
            self:DOMiLogin(WebLoginMan.ERequestType.Login)
        end
    elseif name =="btn_selectserver" then
        self:ShowStage(2)
    end

    if name == "btn_notice" then
        self.maskGo:SetActive(true)
        self:ShowSDKNotify(true)
        -- ECPanelNotifyMsg.Instance():DownloadNotice(ECPanelNotifyMsg.ENotifyType.UpdateNotify,function (  )
        --     self.maskGo:SetActive(false)
        -- end)
    end
    if name == "btn_user" then
        if KSSDKProxy.Instance():SDKEnable() then
            KSSDKProxy.Instance():ShowUserCenter()
        end
    end
    if name == "btn_switch" then
        if KSSDKProxy.Instance():SDKEnable() then
            KSSDKProxy.Instance():Logout()
        end
    end
end

--登录阶段设置1登录账号  2 选择服务器 3进入游戏
def.method("number").ShowStage = function(self,state)
    self:ShowLogin()
    self:ShowServerList(state==2)
end
def.override("boolean").OnShow = function(self,show)
    if show then
        --切换角色时在重新登录的过程中注册了登录成功的消息，如果切换角色失败，在这里注销该消息
        Event.UnregisterEvent(ModuleId.Login, gmodule.notifyId.Login.LOGIN_SUCCESS, ECGame.ChangeRoleLoginSuccess)
        Event.RegisterEvent(ModuleId.Login, gmodule.notifyId.Login.LOGIN_SUCCESS, UILogin.LoginSuccess);
        Event.RegisterEvent(ModuleId.Login, gmodule.notifyId.Login.LOGIN_TOSTART, UILogin.LoginToStart);
    else
        Event.UnregisterEvent(ModuleId.Login, gmodule.notifyId.Login.LOGIN_SUCCESS, UILogin.LoginSuccess);
        Event.UnregisterEvent(ModuleId.Login, gmodule.notifyId.Login.LOGIN_TOSTART, UILogin.LoginToStart);
    end
end

def.override().OnDestroy = function (self)
    self.m_loginObj:unbind() 
    self.m_inputAccount:unbind() self.m_inputAccount = nil
    self.m_inputPassword:unbind() self.m_inputPassword = nil
    self.m_fastToggle:unbind()
    self.m_openNetToggle:unbind()
    self.m_serverText:unbind()
    self.m_serverState:unbind() self.m_serverState = nil
    
    self.m_login_btn_pc:unbind()
    self.m_login_btn_ks:unbind()

    self.btn_notice:unbind()
    self.btn_notice = nil
    self.btn_user:unbind()
    self.btn_user = nil
    self.btn_switch:unbind()
    self.btn_switch = nil
    if IsAutoTest then
        GAutomator.Instance:UnRigisterCallBack("AutoSetIp")
    end
    GameObject.Destroy(self.m_loginBg)
    self.m_loginBg:unbind() self.m_loginBg=nil
    UILogin.Instance():ShowSDKNotify(false)
    local UIServerList = require "Game.Login.ui.UIServerList"
    UIServerList.Instance():DestroyPanel()

    m_Instance = nil

end

def.static("table", "table").LoginSuccess = function(p1, p2)
    Event.DispatchEvent(ModuleId.Login,gmodule.notifyId.Login.SERVER_SUCCESS,nil)
end

def.static("table", "table").LoginToStart = function(p1, p2)
    local self = m_Instance
    if not self then
        UILogin.Instance():CreateUI(nil)
    else
        self:ShowStage(1)
    end
end

UILogin.Commit()

return UILogin