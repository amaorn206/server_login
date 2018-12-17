#include "sys_string.h"
#include "login_svc.h"
#include "token_generator.h"
#include "playermgr.h"
#include "auth_svc_proxy.h"
#include "db_svc_proxy.h"
#include "server_conf.h"
#include "svclogger.h"
#include "char_default_assets.h"
#include "date_time.h"
#include "db_redis_agent.h"
#include "rapidjson/document.h"
#include <sstream>
#include "statistical_svc_proxy.h"
#include "role_svc_proxy.h"
#include "bi_svc_proxy.h"
#include "login_queue.h"
#include "game_svc_proxy.h"
#include "whitelist/whitelist_mgr.h"
#include "protodef/protocol_game_entity.pb.h"
#include "sensitive_words/shield_font_assets.h"
#include "sensitive_words/sensitive_words.h"
#include "peer.h"
#include "role_svc_proxy.h"

const int64_t MAX_CHAR_FROZEN_TIME = (48*60*60);

login_svc_t::login_svc_t()
	: _se_svr(NULL)
  , _port(0)
  , _node_id(-1)
  , _charguid_gen(NULL) {
	  util::http_client_t::global_init();
}

login_svc_t::~login_svc_t() {
	release();
	util::http_client_t::global_fini();
}

int login_svc_t::init(const svc_init_t& init) {
	_addr = init.addr;
	_port = init.port;
	_node_id = init.node_id;

	_se_svr = new se_server_t(init.reactor);
	if (!_se_svr) {
		return (-1);
	}

	_charguid_gen = new token_generator_t(_node_id);
	if (!_charguid_gen) {
		return (-1);
	}

	if (defaultchar_assets_t::load("config/char_default_cfg.bin") != 0) {
		return (-1);
	}
	if (shieldfont_assets_t::load())
		return (-1);
	const redis_conf_t& redis_conf = server_comm_cfg::instance().redis_conf();
	
	redisconf_t redisconf;
	redisconf._reactor = init.reactor;
	redisconf.addr = redis_conf.addr();
	redisconf.pwd = redis_conf.password();
	redisconf.conn_timeout = redis_conf.conntimeout();
	redisconf.rw_timeout = redis_conf.rwtimeout();
	redisconf.sleep_msec = redis_conf.sleepmsec();
	redisconf.reconn_msec = redis_conf.reconnmsec();
	if (db_redis_agent::instance().open(redisconf) != 0) {
		SVCLOG_ERR("open redis %s failed", redisconf.addr.c_str());
		return (-1);
	}
	SVCLOG_INFO("open redis %s [OK]", redisconf.addr.c_str());

	whitelist_mgr::instance().init();

	register_handlers();

// 	for (int i= 0; i< 10000; i++)
// 	{
// 		token_t token = 25518333524705280L+ i;
// 		login_test(token);
// 	}
	return (0);
}

void login_svc_t::login_test(token_t token)
{
	std::string url  = "http://192.168.1.208:8080/api/kaisa_login_server";
	
	std::string back_param = "";
	char param [512] = {0};
	sprintf(param,"tf_token=%lld&server_id=1&app_id=100000026", (long long)token);
	_http_client.http_post(url,param,back_param);
}
void login_svc_t::release() {
	if (_se_svr) _se_svr->release();
	SAFE_DELETE(_se_svr);

	SAFE_DELETE(_charguid_gen);
}

int login_svc_t::start() {
	se_srv_init_t init;
	init.addr = _addr;
	init.port = _port;
	init.handler = &_proto_handler;
	if (_se_svr->init(init) != 0) {
		return (-1);
	}
	return (0);
}

player_t* login_svc_t::get_acc_player(user_id_t user_id) const {
	acc_players_t::const_iterator it = _acc_players.find(user_id);
	if (it != _acc_players.end())
		return it->second;
	return (NULL);
}

player_t* login_svc_t::get_token_player(token_t token) const {
	token_players_t::const_iterator it = _token_players.find(token);
	if (it != _token_players.end())
		return it->second;
	return (NULL);
}

int login_svc_t::acclogin(ns::packprotocol_t &conn, token_t token,  const ftproto::DeviceInfo &  device_info) {

	SVCLOG_DEBUG("acclogin token = %lld ",token);
	session_t* real_sess = (session_t*)(&conn);
	if (real_sess&& real_sess->token() != INVALID_TOKEN)
	{
		SVCLOG_ERR("sess has token = %lld and new token = %lld",real_sess->token(),token);
		return (-1);
	}
	player_t* player = get_token_player(token);
	if (player)
	{
		SVCLOG_DEBUG("acclogin token = %lld already login on",token);
		notify_game_kick_player(player->user_id());
		reply_acclogin(conn, ftproto::ErrorCode::ERR_LOGIN_LOGIN_TOKEN, token);
		player_disconnect(player);
		session_disconnect(&conn);
		return (-1);
	}
	player = player_mgr::instance().alloc();
	if (!player)
	{
		SVCLOG_DEBUG("can not alloc player = %lld ",token);
		reply_acclogin(conn, ftproto::ErrorCode::ERR_LOGIN_INVALID_ACCOUNT, token);
		session_disconnect(&conn);
		return (-1);
	}
	
	real_sess->token(token);
	player->init();
	player->set_token(token);
	add_token_player(*player);
	player->login_time(util::date_time_t::cur_srv_sectime());
	player->set_device_info(device_info);
	player->set_sess(&conn);
	accauth(player);
	return (0);
}

void login_svc_t::reply_acclogin(ns::packprotocol_t &conn, int errcode,token_t token) {
	ftproto::LCAccLogin msg;
	msg.set_errcode((ftproto::ErrorCode)errcode);
	if (token != INVALID_TOKEN)
		msg.set_token(token);
	std::string buf;
	msg.SerializeToString(&buf);
	conn.send(msg.msgid(), buf.c_str(), buf.size());
}

int login_svc_t::accauth( player_t* player )
{
	if (!player)
	{
		SVCLOG_DEBUG("accauth player is null");
		return (-1);
	}
	server_id_t srv_id = server_comm_cfg::instance().srv_id();
	app_id_t app_id =   server_comm_cfg::instance().app_id();

	std::stringstream param;
	param << "tf_token=";
	param << player->token();
	param << "&server_id=";
	param << srv_id;
	param << "&app_id=";
	param << app_id;

	std::string url  = server_comm_cfg::instance().check_url();

	std::stringstream back_param;
	back_param << player->token();

	//_http_client.http_post<login_svc_t>(url, param.str(), &login_svc_t::auth_token_callback, this, back_param.str());

	this->auth_token_callback( (CURLcode)0,"",back_param.str());

	SVCLOG_DEBUG("auth token player = %lld ",player->token());
	return (0);
}

void login_svc_t::auth_token_callback( CURLcode code,const std::string& response,const std::string& back_param )
{
	token_t token;
	std::stringstream str_token;
	str_token << back_param;
	str_token >> token;

	player_t* player = get_token_player(token);
	/*
	if (!player) 
	{
		SVCLOG_DEBUG("auth_token_callback token player = %lld and can not find player",token);
		return ;
	}
	if (!player->sess()) 
	{
		SVCLOG_DEBUG("auth_token_callback token player = %lld and player has no conn",token);
		player_disconnect(player);
		return ;
	}
	*/

	if (code == CURLE_OK)
	{
		/*
		rapidjson::Document doc;
		doc.Parse(response.c_str());
		
		if (doc.HasParseError()|| !doc.IsObject() || !doc.HasMember("code")) 
		{
			//格式错误
			reply_acclogin(*(player->sess()), ftproto::ErrorCode::ERR_SYSTEM, token);
			player_disconnect(player);
			return ;
		}
		int64_t auth_code = doc["code"].GetInt64();
		if (auth_code != 10000) 
		{
			////验证失败；
			reply_acclogin(*(player->sess()), ftproto::ErrorCode::ERR_AUTH_FAILED, token);
			player_disconnect(player);
			return ;
		}

		rapidjson::Value& auth_data = doc["data"];
		if (!auth_data.IsObject() 
			|| !auth_data.HasMember("tf_id")
			|| !auth_data.HasMember("device_type")
			|| !auth_data.HasMember("channel_id")
			|| !auth_data.HasMember("uid")
			|| !auth_data.HasMember("is_create")) 
		{
			reply_acclogin(*(player->sess()), ftproto::ErrorCode::ERR_SYSTEM, token);
			player_disconnect(player);
			return ;
		}


		int32_t os_type = auth_data["device_type"].GetInt();
		user_id_t user_id = (user_id_t)auth_data["tf_id"].GetUint64();
		channel_id_t channel_id = auth_data["channel_id"].GetInt();
		std::string uid = auth_data["uid"].GetString();
		int32_t is_create = auth_data["is_create"].GetInt();
		*/

		int32_t os_type = 0;
		user_id_t user_id = 11440;
		channel_id_t channel_id = 100000;
		std::string uid = "bb12";
		int32_t is_create = 0;

		/*
		if(!whitelist_mgr::instance().allow(uid))
		{
			reply_acclogin(*(player->sess()), ftproto::ErrorCode::ERR_LOGIN_WHITENAME_LIMIT, token);
			player_disconnect(player);
			return;
		}
		*/

		player->channel_uid(uid);
		player->channel_id(channel_id);
		
		if (os_type == 1)
		{
			player->os_type(DeviceOSTypeApple);
		}
		else
		{
			player->os_type(DeviceOSTypeAndroid);
		}
		player->user_id(user_id);
		player->is_create(is_create);
		SVCLOG_DEBUG("auth_token_callback token player = %lld and user_id = %lld",token,user_id);
		kick_last_role(player);
		add_acc_player(*player);

		reply_acclogin(*(player->sess()), ftproto::ErrorCode::ERR_OK,token);
	}
	else
	{
		reply_acclogin(*(player->sess()), ftproto::ErrorCode::ERR_LOGIN_AUTH_FAILED, token);
		player_disconnect(player);
	}
}

int login_svc_t::charlist(ns::packprotocol_t &conn, token_t token) {
	player_t* player = get_token_player(token);
	if (!player) {
		reply_charlist(conn, ftproto::ErrorCode::ERR_LOGIN_USER_LOAD);
		SVCLOG_INFO("login_svc_t charlist no player for token %lld", (long long)token);
		return (0);
	}
	util::deferred_t* d = db_svc_proxy::instance().charlist(player->user_id());
	if (!d) {
		SVCLOG_ERR("login_svc_t db_svc_proxy_t charlist failed token %lld", (long long)token);
		return (-1);
	}
	d->add_callback<login_svc_t>(&login_svc_t::charlist_res, this);
	return (0);
}

void login_svc_t::reply_charlist(ns::packprotocol_t &conn, int errcode, const charlist_t& charlist) {
	ftproto::LCCharList msg;
	msg.set_errcode((ftproto::ErrorCode)errcode);
	charlist_t::const_iterator it;
	for (it = charlist.begin(); it != charlist.end(); ++it) {
		const char_base_info_t& info = it->second;
		ftproto::CharacterBaseInfo* pr_info = msg.add_charlist();
		pr_info->set_charguid(info.charguid);
		pr_info->set_charname(info.charname);
		pr_info->set_profession((ftproto::EProfessionType)info.profession);
		pr_info->set_gender((ftproto::EGenderType)info.gender);
		pr_info->set_camp((ftproto::ECampType)info.camp);
		pr_info->set_level(info.level);
		pr_info->set_exp(info.exp);
		pr_info->set_frozen(info.frozen);
		pr_info->set_freezetime(info.freezetime);
		pr_info->set_ce(info.ce);
		pr_info->set_delete_state(info.delete_state);
		pr_info->set_deletetime(info.delete_time);
		pr_info->set_fashion_clothesid(info.fashion_clothesid);
		pr_info->set_fashion_weaponid(info.fashion_weaponid);
		pr_info->set_fashion_mountid(info.fashion_mountid);
		pr_info->set_fashion_wingid(info.fashion_wingid);
	}
	std::string buf;
	msg.SerializeToString(&buf);
	conn.send(msg.msgid(), buf.c_str(), buf.size());
}

void* login_svc_t::charlist_res(void *result, void *param) {
	if (!result)
		return (NULL);

	ftproto::DLCharList* msg = (ftproto::DLCharList*)result;
	//int errcode = msg->errcode();
	user_id_t user_id = msg->user_id();

	player_t* player = get_acc_player(user_id);
	if (!player) {
		return (NULL);
	}
	if (!player->sess()) {
		return (NULL);
	}

	player->clear_charlist();
	time_t curtime = util::date_time_t::cur_srv_sectime();
	const ftproto::CharacterBaseInfos& charlist = msg->charlist();
	int i;
	for (i = 0; i < charlist.infos_size(); ++i) {
		char_base_info_t info;
		info.charguid = charlist.infos(i).charguid();
		info.charname = charlist.infos(i).charname();
		info.profession = charlist.infos(i).profession();
		info.gender = charlist.infos(i).gender();
		info.camp = charlist.infos(i).camp();
		info.level = charlist.infos(i).level();
		info.exp = charlist.infos(i).exp();
		info.frozen = charlist.infos(i).frozen();
		info.freezetime = charlist.infos(i).freezetime();
		info.delete_state = charlist.infos(i).delete_state();
		info.fashion_clothesid = charlist.infos(i).fashion_clothesid();
		info.fashion_weaponid = charlist.infos(i).fashion_weaponid();
		info.fashion_mountid = charlist.infos(i).fashion_mountid();
		info.fashion_wingid = charlist.infos(i).fashion_wingid();
		// 角色被删除状态判断删除时间
 		if (info.delete_state) {
			int64_t last_time = charlist.infos(i).deletetime() + MAX_CHAR_FROZEN_TIME - curtime;
 			info.delete_time = last_time > 0 ?last_time:0;
 		}
		// 检查封禁时间
		if(info.frozen)
		{
			if(curtime > info.freezetime)
			{
				info.frozen		= 0;
				info.freezetime = 0;

				// 更新DB状态
				db_svc_proxy::instance().unbanchar(user_id, info.charguid);
			}
		}
 		info.ce = charlist.infos(i).ce();
 		if (info.delete_state && 0 == info.delete_time)
 			continue;
		player->add_charinfo(info);
		SVCLOG_DEBUG("fetch char guid = %lld", (long long)charlist.infos(i).charguid());
	}
	reply_charlist(*(player->sess()), ftproto::ErrorCode::ERR_OK, player->charlist());

	//创建账号log
	if (player->is_create())
	{
		//log create
		tagAcountcreate log;
		player->fillLogBaseData(0,log.basedata);
		log.basedata.charguid	= 0;
		log.ip = player->get_device_info().ip();

		statistical_svc_proxy::instance().create_acc(log);
		player->is_create(0);
	}

	if (player->get_create_role() != INVALID_CHAR_GUID)
	{
		tag_role_info_t role_info;
		player->fill_role_info(player->get_create_role(), role_info);
		role_svc_proxy::instance().role_info(role_info);

		player->set_create_role(INVALID_CHAR_GUID);
	}

	//---log begin	----->
	//需求要得到账号角色数，所以账号登录换了地方，写在了这里
	std::string ipd = "";
	ipd =  player->get_device_info().ip();
	tagLogAccLogin aclog;
	player->fillLogBaseData(0,aclog.basedata);
	aclog.game_version = player->get_device_info().game_version();
	aclog.os_version = player->get_device_info().os_version();
	aclog.operators = player->get_device_info().operators();
	std::stringstream stream;  
	stream<<player->get_device_info().net_type();
	aclog.net_type			= stream.str();
	aclog.ppi = player->get_device_info().ppi();	
	aclog.charcount = player->charlist().size();

	statistical_svc_proxy::instance().account_login(aclog);
	//<--log end	-----
	return (NULL);
}

int utf8_char_count(const char* input, bool& allchar)//汉字长度：2，字母数字长度：1
{
	int output_size = 0; //记录转换后的Unicode字符串的字节数
	if (!input)
		return -1;
	while (*input)
	{
		if (*input > 0x00 && *input <= 0x7F) //处理单字节UTF8字符（英文字母、数字）
		{
			output_size += 1;
			input += 1;
		}
		else
		{
			if (((*input) & 0xE0) == 0xC0)
			{
				input += 2;
			}
			else if (((*input) & 0xF0) == 0xE0)
			{
				input += 3;
			}
			else if (((*input) & 0xF8) == 0xF0)
			{
				input += 4;
			}
			else if (((*input) & 0xFC) == 0xF8)
			{
				input += 5;
			}
			else if (((*input) & 0xFE) == 0xFC)
			{
				input += 6;
			}
			output_size += 2;
			allchar = false;
		}
	}

	return output_size;
}

int createchar_errorcode(int ec)
{
	int ret = ftproto::ErrorCode::ERR_SYSTEM;
	switch (ec)
	{
	case (check_error::ERROR_SYMBOL) :
		{
			ret = ftproto::ErrorCode::ERR_NAME_HAS_SYMBOL;
		}break;
	case (check_error::ERROR_SENSITIVEWORDS) :
		{
			ret = ftproto::ErrorCode::ERR_NAME_CHARACTER_UNLAWFUL;
		}break;
	default:
		break;
	}
	return ret;
}

int login_svc_t::createchar(ns::packprotocol_t &conn, token_t token, const char* charname, profession_t profession, gender_t gender) {
	player_t* player = get_token_player(token);
	if (!player) {
		reply_createchar(conn, ftproto::ErrorCode::ERR_LOGIN_CREATE_LOAD);
		SVCLOG_INFO("login_svc_t createchar no player for token %lld", (long long)token);
		return (0);
	}

	const defaultcharcfg_t* cfg = defaultchar_assets_t::get_cfg(profession, gender);
	if (!cfg) {
		reply_createchar(conn, ftproto::ErrorCode::ERR_LOGIN_CREATE_CONF);
		SVCLOG_INFO("login_svc_t createchar no default char cfg for token %lld profession %d gender %d", (long long)token, profession, gender);
		return (0);
	}
	bool allchar = true;
	int length = utf8_char_count(charname, allchar);
	if (length > (int)cfg->name_max_length())
	{
		reply_createchar(conn, ftproto::ErrorCode::ERR_NAME_LENGTH_UNLAWFUL);
		SVCLOG_INFO("login_svc_t createchar name too long for token %lld profession %d gender %d", (long long)token, profession, gender);
		return (0);
	}

	sensitive_check_t sc;
	int type = check_type::ETYPE_ASC;
	if (allchar)
		type = check_type::ETYPE_MATCHWHOLEWORD;
	int ret = sc.check_utf8(charname, type);
	if (ret != check_error::ERROR_OK)
	{
		reply_createchar(conn, createchar_errorcode(ret));
		SVCLOG_INFO("login_svc_t createchar name has symbol or sensitive words for token %lld profession %d gender %d", (long long)token, profession, gender);
		return (0);
	}

	charguid_t charguid = _charguid_gen->gen_id();

	createchar_info_t info;
	info.charguid = charguid;
	std::string strCharGuid;
	NUM2STR(strCharGuid,  charguid);
	info._user_id = player->user_id();
	info._channel_id = player->channel_id();
	info._os_type = player->os_type();
	info.charname = charname;
	info.profession = profession;
	info.gender = gender;
	info.level = cfg->level();
	info.exp = cfg->exp();
	info.mapid = cfg->map_id();
	info.pos = cfg->pos();
	info.createtime = util::date_time_t::cur_srv_sectime();
	info.power = cfg->initpower();
	info.channel_uid = player->channel_uid();
	util::deferred_t* d = db_svc_proxy::instance().createchar(player->user_id(), info);
	if (!d) {
		return (-1);
	}
	d->add_callback<login_svc_t>(&login_svc_t::createchar_res, this);

	return (0);
}

void login_svc_t::reply_createchar(ns::packprotocol_t &conn, int errcode) {
	ftproto::LCCreateChar msg;
	msg.set_errcode((ftproto::ErrorCode)errcode);
	std::string buf;
	msg.SerializeToString(&buf);
	conn.send(msg.msgid(), buf.c_str(), buf.size());
}

void* login_svc_t::createchar_res(void *result, void *param) {
	if (!result)
		return (NULL);

	ftproto::DLCreateChar* msg = (ftproto::DLCreateChar*)result;
	int errcode = msg->errcode();
	user_id_t user_id = msg->user_id();

	player_t* player = get_acc_player(user_id);
	if (!player) {
		return (NULL);
	}
	if (!player->sess()) {
		return (NULL);
	}

	if (ftproto::ErrorCode::ERR_OK == errcode) {
		player->set_create_role(msg->char_guid());

		charlist(*(player->sess()), player->token());
		//log
		log_create_char(player,msg);
	}

	reply_createchar(*(player->sess()), errcode);
	
	return (NULL);
}

#define FREEZE_LEVEL 10
int login_svc_t::deletechar(ns::packprotocol_t &conn, token_t token, charguid_t charguid) {
	player_t* player = get_token_player(token);
	if (!player) {
		reply_deletechar(conn, ftproto::ErrorCode::ERR_SYSTEM);
		return (0);
	}
	const char_base_info_t* info = player->get_char(charguid);
	if (!info) {
		reply_deletechar(conn, ftproto::ErrorCode::ERR_SYSTEM);
		return (0);
	}
	if (1 == info->delete_state) {
		reply_deletechar(conn, ftproto::ErrorCode::ERR_SYSTEM);
		return (0);
	}
	time_t deletetime = util::date_time_t::cur_srv_sectime();
	
	util::deferred_t* d = NULL;
	if (info->level >= FREEZE_LEVEL)
		d = db_svc_proxy::instance().freezechar(player->user_id(), charguid, deletetime);
	else
		d = db_svc_proxy::instance().deletechar(player->user_id(), charguid, deletetime);
	if (!d) {
		return (-1);
	}
	d->add_callback<login_svc_t>(&login_svc_t::deletechar_res, this);
	
	
	return (0);
}

void login_svc_t::reply_deletechar(ns::packprotocol_t &conn, int errcode) {
	ftproto::LCDeleteChar msg;
	msg.set_errcode((ftproto::ErrorCode)errcode);
	std::string buf;
	msg.SerializeToString(&buf);
	conn.send(msg.msgid(), buf.c_str(), buf.size());
}

void* login_svc_t::deletechar_res(void *result, void *param) {
	if (!result)
		return (NULL);

	ftproto::DLDeleteChar* msg = (ftproto::DLDeleteChar*)result;
	int errcode = msg->errcode();
	user_id_t user_id = msg->user_id();
	charguid_t charguid = msg->charguid();

	player_t* player = get_acc_player(user_id);
	if (!player) {
		return (NULL);
	}
	if (!player->sess()) {
		return (NULL);
	}

	reply_deletechar(*(player->sess()), errcode);

	if (ftproto::ErrorCode::ERR_OK == errcode) {
		const char_base_info_t* info = player->get_char(charguid);
		if (info) {
			stLogCharLogin  logcharlogin;
			player->fillLogBaseData(charguid,logcharlogin.basedata);
			logcharlogin.profession = info->profession;
			logcharlogin.gender = info->gender;
			logcharlogin.ip = player->get_device_info().ip();
			logcharlogin.charname = info->charname;
			logcharlogin.exp = info->exp;
			logcharlogin.role_level = info->level;
			logcharlogin.gameversion = player->get_device_info().game_version();
			statistical_svc_proxy::instance().player_delete(logcharlogin);
		}
		
		charlist(*(player->sess()), player->token());

		tag_role_info_t role_info;
		player->fill_role_info(charguid, role_info);
		role_info.is_delete = 1;
		role_svc_proxy::instance().role_info(role_info);
	}

	return (NULL);
}

int login_svc_t::cancelfreezechar(ns::packprotocol_t &conn, token_t token, charguid_t charguid) {
	player_t* player = get_token_player(token);
	if (!player) {
		reply_cancelfreezechar(conn, ftproto::ErrorCode::ERR_SYSTEM);
		return (0);
	}
	const char_base_info_t* info = player->get_char(charguid);
	if (!info) {
		reply_cancelfreezechar(conn, ftproto::ErrorCode::ERR_SYSTEM);
		return (0);
	}
	if (0 == info->delete_state) {
		reply_cancelfreezechar(conn, ftproto::ErrorCode::ERR_SYSTEM);
		return (0);
	}
	util::deferred_t* d = db_svc_proxy::instance().cancelfreezechar(player->user_id(), charguid);
	if (!d) {
		return (-1);
	}
	d->add_callback<login_svc_t>(&login_svc_t::cancelfreezechar_res, this);
	return (0);
}

void login_svc_t::reply_cancelfreezechar(ns::packprotocol_t &conn, int errcode) {
	ftproto::LCCancelFreezeChar msg;
	msg.set_errcode((ftproto::ErrorCode)errcode);
	std::string buf;
	msg.SerializeToString(&buf);
	conn.send(msg.msgid(), buf.c_str(), buf.size());
}

void* login_svc_t::cancelfreezechar_res(void *result, void *param) {
	if (!result)
		return (NULL);

	ftproto::DLCancelFreezeChar* msg = (ftproto::DLCancelFreezeChar*)result;
	int errcode = msg->errcode();
	user_id_t user_id = msg->user_id();

	player_t* player = get_acc_player(user_id);
	if (!player) {
		return (NULL);
	}
	if (!player->sess()) {
		return (NULL);
	}

	reply_cancelfreezechar(*(player->sess()), errcode);

	if (ftproto::ErrorCode::ERR_OK == errcode) {
		charlist(*(player->sess()), player->token());

		tag_role_info_t role_info;
		player->fill_role_info(msg->charguid(), role_info);
		role_info.is_delete = 0;
		role_svc_proxy::instance().role_info(role_info);
	}

	return (NULL);
}

int login_svc_t::charlogin(ns::packprotocol_t &conn, token_t token, charguid_t charguid,std::string& version) {
	player_t* player = get_token_player(token);
	if (!player) {
		reply_charlogin(conn, ftproto::ErrorCode::ERR_LOGIN_USER_LOAD);
		return (0);
	}

	//<检查客户端版本
	if(!check_clietn_version(version))
	{
		reply_charlogin(conn,ftproto::ErrorCode::ERR_LOGIN_CLINENT_VERSION);
		return -1;
	}

	if(!whitelist_mgr::instance().allow(player->channel_uid()))
	{
		reply_charlogin(conn,ftproto::ErrorCode::ERR_LOGIN_WHITENAME_LIMIT);
		return -1;
	}

	const char_base_info_t* info = player->get_char(charguid);
	if (!info) {
		reply_charlogin(conn, ftproto::ErrorCode::ERR_LOGIN_USER_BASEINFO_INVALID);
		return (0);
	}

	bool freeze_flag = false;
	// 角色被冻结 
	if (1 == info->frozen) {
		// 检查封号时长
		time_t curtime = util::date_time_t::cur_srv_sectime();

		if(curtime < info->freezetime)
		{
			reply_charlogin(conn, ftproto::ErrorCode::ERR_PLAYER_BE_FROZEN);
			return (0);
		}
		else
		{
			db_svc_proxy::instance().unbanchar(player->user_id(), charguid);
			freeze_flag = true;
		}
	}

// 	// 已在线
// 	if(freeze_flag == false)
// 	{
// 		if ( is_online(player, charguid) )
// 		{
// 			notify_game_user_login(player, charguid);
// 
// 			return 0;
// 		}
// 	}

	// 先排队
	if (login_queue_mgr::instance().start_queue(player->user_id(), charguid) != 0) {
		reply_charlogin(conn, ftproto::ErrorCode::ERR_LOGIN_QUEUE);
		return (0);
	}

	if ( is_on_redis(player, charguid) )
	{
		notify_game_user_login(player, charguid);

		return 0;
	}

	return char_real_login(player, charguid);
}

int login_svc_t::char_real_login( player_t* player, charguid_t charguid )
{
	if (!player || INVALID_CHAR_GUID == charguid)
		return -1;

	util::deferred_t* d = db_svc_proxy::instance().loadfullchar(player->user_id(), charguid);
	if (!d) {
		SVCLOG_ERR("login_svc_t db_svc_proxy_t loadfullchar failed token %lld", (long long)player->token());
		return -1;
	}
	d->add_callback<login_svc_t>(&login_svc_t::fullchar_res, this);

	return 0;
}

void login_svc_t::reply_charlogin(ns::packprotocol_t &conn, int errcode, const char* ip, int32_t port) {
	ftproto::LCCharLogin msg;
	msg.set_errcode((ftproto::ErrorCode)errcode);
	if (ftproto::ERR_OK == errcode) {
		ftproto::NetAddr* addr = msg.mutable_addr();
		addr->set_ip(ip);
		addr->set_port(port);
	}
	std::string buf;
	msg.SerializeToString(&buf);
	conn.send(msg.msgid(), buf.c_str(), buf.size());
}

void* login_svc_t::fullchar_res(void *result, void *param) {
	if (!result)
		return (NULL);

	ftproto::DLLoadFullChar* msg = (ftproto::DLLoadFullChar*)result;
	int errcode = msg->errcode();
	user_id_t user_id = msg->user_id();
	charguid_t charguid = msg->charguid();

	login_queue_mgr::instance().stop_loading( user_id );

	player_t* player = get_acc_player(user_id);
	if (!player) {
		return (NULL);
	}
	if (!player->sess()) {
		return (NULL);
	}

	const srvconf_t* gameconf = server_comm_cfg::instance().get_srvconf("gamesrv");
	if (!gameconf) {
		reply_charlogin(*(player->sess()), ftproto::ErrorCode::ERR_LOGIN_SERVER_CONF);
		return (NULL);
	}

	if (errcode != ftproto::ErrorCode::ERR_OK) {
		reply_charlogin(*(player->sess()), ftproto::ErrorCode::ERR_LOGIN_LOAD_FROM_DB);
		return (NULL);
	}

	//缓存fullchar
	const ftproto::FullCharInfo& fullchar = msg->fullchar();
	char hname[128] = {0};
	tsnprintf(hname, 128, "fullchar_%lld", (long long)charguid);

	std::string hval;
	fullchar.SerializeToString(&hval);
	acl::acl_redis_t* redis = db_redis_agent::instance().getconn();
	if (!redis || (!redis->psetex(hname,strlen(hname),hval.c_str(),hval.size(), FULLCHAR_TIME))) {
		reply_charlogin(*(player->sess()), ftproto::ErrorCode::ERR_LOGIN_REDIS_DATA);
		return (NULL);
	}
	SVCLOG_DEBUG("fullchar_res loadfullchar guid = %lld", (long long)fullchar.character().charguid());
	notify_game_user_login(player, charguid);
	
	return (NULL);
}

int login_svc_t::add_acc_player(player_t& player) {
	if (player.user_id() != INVALID_USER_ID) {
		acc_players_t::iterator it = _acc_players.find(player.user_id());
		if (it != _acc_players.end())
		{
			it->second = &player;
		}
		else
		{
			_acc_players.insert(acc_players_t::value_type(player.user_id(), &player));
		}
	}
	return (0);
}

int login_svc_t::del_acc_player(user_id_t userid) {
	_acc_players.erase(userid);
	return (0);
}

int login_svc_t::add_token_player(player_t& player) {
	if (player.token() != INVALID_TOKEN) {
		token_players_t::iterator it = _token_players.find(player.token());
		if ( it != _token_players.end() ) {
			it->second = &player;
		} else {
			_token_players.insert(token_players_t::value_type(player.token(), &player));
		}
	}
	
	return (0);
}

int login_svc_t::del_token_player(token_t token) {
	_token_players.erase(token);
	return (0);
}

void login_svc_t::register_handlers() {
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_ACCLOGIN, &login_svc_t::handler_CLAccLogin, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_CHARLIST, &login_svc_t::handler_CLCharList, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_CREATECHAR, &login_svc_t::handler_CLCreateChar, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_DELETECHAR, &login_svc_t::handler_CLDeleteChar, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_CANCEL_FREEZECHAR, &login_svc_t::handler_CLCancelFreezeChar, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_CHARLOGIN, &login_svc_t::handler_CLCharLogin, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_LC_GET_QUEUE_USER_COUNT, &login_svc_t::handler_CLGetLoginQueueInfo, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMsgID_CL_CANCEL_LOGIN_QUEUE, &login_svc_t::handler_CLCancelLoginQueue, this);
	_proto_handler.register_handler<login_svc_t>(ftproto::EMsgID::EMSGID_CL_OPERATION_FRONT, &login_svc_t::handler_CLOperationFront, this);
}

int login_svc_t::handler_CLAccLogin(ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size) {
	ftproto::CLAccLogin msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}
	std::string str = msg.token();
	std::stringstream param;
	param << str;
	token_t token;
	param >> token;
	const ftproto::DeviceInfo & device_info = msg.device_info();
	return acclogin(conn,token, device_info);
}

int login_svc_t::handler_CLCharList(ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size) {
	ftproto::CLCharList msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}
	token_t token = msg.token();
	return charlist(conn, token);
}

int login_svc_t::handler_CLCreateChar(ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size) {
	ftproto::CLCreateChar msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}
	token_t token = msg.token();

	const char* charname = msg.charname().c_str();
	if (!charname || !strlen(charname))
	{
		reply_createchar(conn, ftproto::ErrorCode::ERR_NAME_NULL);
		return -1;
	}
	profession_t profession = msg.profession();
	gender_t gender = msg.gender();
	return createchar(conn, token, charname, profession, gender);
}

int login_svc_t::handler_CLDeleteChar(ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size) {
	ftproto::CLDeleteChar msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}
	token_t token = msg.token();
	charguid_t charguid = msg.charguid();
	return deletechar(conn, token, charguid);
}

int login_svc_t::handler_CLCancelFreezeChar(ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size) {
	ftproto::CLCancelFreezeChar msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}
	token_t token = msg.token();
	charguid_t charguid = msg.charguid();
	return cancelfreezechar(conn, token, charguid);
}

int login_svc_t::handler_CLCharLogin(ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size) {
	ftproto::CLCharLogin msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}
	token_t token = msg.token();
	charguid_t charguid = msg.charguid();
	std::string str_version = msg.client_version();
	return charlogin(conn, token, charguid,str_version);
}

int login_svc_t::handler_CLGetLoginQueueInfo( ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size )
{
	ftproto::CLGetLoginQueueInfo msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}

	player_t* player = get_token_player(msg.token());
	if (!player) {
		return (-1);
	}

	ftproto::LCLoginQueueInfo ret_msg;
	ret_msg.set_pos( login_queue_mgr::instance().queue_index(player->user_id()) );
	ret_msg.set_total_count(login_queue_mgr::instance().queue_size());

	std::string buf;
	ret_msg.SerializeToString(&buf);
	conn.send( ret_msg.msgid(), buf.c_str(), buf.size() );

	return 0;
}

int login_svc_t::handler_CLCancelLoginQueue( ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size )
{
	ftproto::CLCancelLoginQueue msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}

	player_t* player = get_token_player(msg.token());
	if (!player) {
		return (-1);
	}

	ftproto::LCCancelLoginQueueResult ret_msg;
	ret_msg.set_errcode( login_queue_mgr::instance().stop_queue(player->user_id()) );

	std::string buf;
	ret_msg.SerializeToString(&buf);
	conn.send( ret_msg.msgid(), buf.c_str(), buf.size() );

	return 0;
}

int login_svc_t::handler_CLOperationFront( ns::packprotocol_t &conn, opcode_t opcode, const void *data, int size )
{
	ftproto::CLOperationFront msg;
	if (!msg.ParseFromArray(data, size)) {
		return (-1);
	}

	player_t* player = get_token_player(msg.token());
	if (!player) {
		return (-1);
	}
	int32_t type = msg.type();
	int32_t mission_id = msg.mission_id();
	int32_t scene_id = msg.scene_id();
	int32_t npc_id = msg.npc_id();

	stLogGuideFront log_guide;
	player->fillLogBaseData(msg.charguid(),log_guide.basedata);

	log_guide.type = type;
	log_guide.mission_id = mission_id;

	log_guide.map_id = scene_id;
	log_guide.npc_id = npc_id;

	statistical_svc_proxy::instance().guide_front(log_guide);

	return 0;
}

bool login_svc_t::check_clietn_version(const std::string& version)
{
	// 客户端PC登陆version为空不校验
	if(version.length() == 0)
	{
		return true;
	}

	std::string srv_config_version  = server_comm_cfg::instance().client_version();
	if(srv_config_version.length() <= 0)
		return false;

	client_version client = splt_version(version);
	client_version server = splt_version(srv_config_version);

	if(client.first == server.first && client.second == server.second && client.third >= server.third)
		return true;
	return false;
}

client_version login_svc_t::splt_version(const std::string& cli)
{
	client_version version;
	sscanf(cli.c_str(),"%d.%d.%d",&version.first,&version.second,&version.third);

	return version;
}

void login_svc_t::tick()
{
	time_t now = util::date_time_t::cur_srv_sectime();
	std::list<player_t*> players;
	// 关闭长时间连接的玩家
	for (token_players_t::iterator it = _token_players.begin(); it != _token_players.end(); ++it)
	{
		if (!it->second->check_online(now))
		{
			players.push_back(it->second);
		}
	}

	whitelist_mgr::instance().tick(now);
	_clear_disconnect_session();
	for (std::list<player_t*>::iterator it = players.begin();it != players.end();++it)
	{
		player_disconnect(*it);
	}
	players.clear();
}

void login_svc_t::notify_game_kick_player(user_id_t user_id )
{
	acc2guid::iterator it_info = _last_role.find(user_id);
	if (it_info != _last_role.end())
	{
		token_role info = it_info->second;
		ftproto::LGKickChar msg;
		msg.set_token(info._token);
		msg.set_charguid(info._charguid);

		std::string buf;
		msg.SerializeToString(&buf);
		game_svc_proxy::instance().send( msg.msgid(), buf.c_str(), buf.size() );
		SVCLOG_DEBUG("notify_game_kick_player token = %lld and charguid = %lld",info._token,info._charguid);
	}
}
void login_svc_t::notify_game_user_login( player_t* player, charguid_t charid )
{
	if (player)
	{
		ftproto::LGTokenChar msg;
		msg.set_token(player->token());
		msg.set_charguid(charid);

		std::string buf;
		msg.SerializeToString(&buf);
		game_svc_proxy::instance().send( msg.msgid(), buf.c_str(), buf.size() );
	}
}

int login_svc_t::notify_client_login( player_t* player, charguid_t charid )
{
	if ( !player || !player->sess() || INVALID_CHAR_GUID == charid )
		return -1;

	const srvconf_t* gameconf = server_comm_cfg::instance().get_srvconf("gamesrv");
	if (!gameconf) {
		reply_charlogin(*(player->sess()), ftproto::ErrorCode::ERR_LOGIN_SERVER_CONF);
		return -1;
	}

	reply_charlogin(*(player->sess()), ftproto::ErrorCode::ERR_OK, gameconf->addr(), gameconf->port());
	player->enter_game();
	player->enter_game_char(charid);

	return 0;
}

bool login_svc_t::is_online( const player_t* player, charguid_t login_char ) const
{
	if (player && login_char != INVALID_CHAR_GUID)
	{
		if ( login_char == player->enter_game_char() )
			return true;

		char hname[128] = {0};
		tsnprintf(hname, sizeof(hname), "fullchar_%lld", (long long)login_char);

		std::string  strFullchar;
		acl::acl_redis_t* redis = db_redis_agent::instance().getconn();
		return (redis->get(hname, strFullchar) > 0);
	}

	return false;
}

bool login_svc_t::is_on_redis( const player_t* player, charguid_t login_char ) const
{
	if (player && login_char != INVALID_CHAR_GUID)
	{
		char hname[128] = {0};
		tsnprintf(hname, sizeof(hname), "fullchar_%lld", (long long)login_char);

		std::string  strFullchar;
		acl::acl_redis_t* redis = db_redis_agent::instance().getconn();
		return (redis->get(hname, strFullchar) > 0);
	}

	return false;
}

void login_svc_t::log_create_char(player_t* player,const ftproto::DLCreateChar* msg)
{
	const defaultcharcfg_t* cfg = defaultchar_assets_t::get_cfg(msg->prof(), msg->gender());
	if (!cfg) 
		return;

	tagBICommonData bicdBICommonData;
	NUM2STR(bicdBICommonData.uid, player->channel_uid());
	NUM2STR(bicdBICommonData.cp_uid, player->user_id());
	NUM2STR(bicdBICommonData.role_id,  msg->char_guid());
	bicdBICommonData.level = cfg->level();
	bicdBICommonData.InitDeviceInfo(player->get_device_info());
	bicdBICommonData.channel = player->channel_id();
	bicdBICommonData.power = cfg->initpower();
	bicdBICommonData.vip_level = 0;
	bicdBICommonData.app_id = server_comm_cfg::instance().app_id();
	time(&bicdBICommonData.time);
	bi_svc_proxy::instance().create_role(bicdBICommonData);

	//create log------->
	stPregister stpcreat;
	player->fillLogBaseData(msg->char_guid(),stpcreat.basedata);	
	stpcreat.createtime  = util::date_time_t::cur_srv_sectime();	
	stpcreat.profession = msg->prof();
	stpcreat.gender = msg->gender();
	stpcreat.ip = player->get_device_info().ip();
	stpcreat.charname = msg->char_name();	
	statistical_svc_proxy::instance().create_role(stpcreat);
	//<-----------end log
}

void login_svc_t::player_disconnect( player_t* player )
{
	if (player == NULL)
	{
		return;
	}
	SVCLOG_INFO("dissconnect player token = %lld and user_id = %lld[OK]", player->token(),player->user_id());
	session_disconnect(player->sess());
	del_token_player(player->token());
	del_acc_player(player->user_id());
	player->fini();
	player_mgr::instance().free(player);
}

void login_svc_t::player_disconnect( token_t token )
{
	player_t* player = get_token_player(token);
	player_disconnect(player);
}

void login_svc_t::_do_disconnect( ns::packprotocol_t *conn )
{
	session_t* sess = (session_t*)conn;
	if (sess) 
	{
		if (sess->is_enable())
		{
			SVCLOG_INFO("dissconnect session token = %lld ", sess->token());
			sess->unenable();
			sess->shut_down("player disconnect");
		}
		ns::peer_t* peer = sess->get_peer();
		peer->close(0);
		SAFE_DELETE(peer);
		SAFE_DELETE(sess);
	}
}

void login_svc_t::_clear_disconnect_session()
{
	session_set_t::iterator it = _delete_sess.begin();

	for (;it != _delete_sess.end();++it)
	{
		ns::packprotocol_t *conn = *it;
		if (conn != NULL)
		{
			_do_disconnect(conn);
		}
	}
	_delete_sess.clear();
}

void login_svc_t::session_disconnect( ns::packprotocol_t *conn )
{
	_delete_sess.insert(conn);
}

void login_svc_t::kick_last_role( player_t* player )
{
	user_id_t user_id = player->user_id();

	// 删除重复账号
	player_t* old_player = get_acc_player(user_id);
	if (old_player && old_player != player)
	{
		SVCLOG_DEBUG("player token change ...");
		player_disconnect(old_player);
	}
	notify_game_kick_player(user_id);
}

void login_svc_t::record_last_role( token_t token )
{
	
	player_t* player = get_token_player(token);
	if (!player)
	{
		return;
	}
	token_role info;
	info._charguid = player->enter_game_char();
	info._token = player->token();

	_last_role[player->user_id()] = info;
}
