
/*
linphone
Copyright (C) 2010  Belledonne Communications SARL 
(simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#ifdef WIN32
#include <time.h>
#endif
#include "linphonecore.h"
#include "sipsetup.h"
#include "lpconfig.h"
#include "private.h"


#include "mediastreamer2/mediastream.h"
#include "mediastreamer2/msvolume.h"
#include "mediastreamer2/msequalizer.h"
#include "mediastreamer2/msfileplayer.h"
#include "mediastreamer2/msjpegwriter.h"

#ifdef VIDEO_ENABLED
static MSWebCam *get_nowebcam_device(){
	return ms_web_cam_manager_get_cam(ms_web_cam_manager_get(),"StaticImage: Static picture");
}
#endif


static MSList *make_codec_list(LinphoneCore *lc, const MSList *codecs, int bandwidth_limit){
	MSList *l=NULL;
	const MSList *it;
	for(it=codecs;it!=NULL;it=it->next){
		PayloadType *pt=(PayloadType*)it->data;
		if (pt->flags & PAYLOAD_TYPE_ENABLED){
			if (bandwidth_limit>0 && !linphone_core_is_payload_type_usable_for_bandwidth(lc,pt,bandwidth_limit)){
				ms_message("Codec %s/%i eliminated because of audio bandwidth constraint.",pt->mime_type,pt->clock_rate);
				continue;
			}
			if (linphone_core_check_payload_type_usability(lc,pt)){
				l=ms_list_append(l,payload_type_clone(pt));
			}
		}
	}
	return l;
}

SalMediaDescription *create_local_media_description(LinphoneCore *lc, LinphoneCall *call){
	MSList *l;
	PayloadType *pt;
	const char *me=linphone_core_get_identity(lc);
	LinphoneAddress *addr=linphone_address_new(me);
	const char *username=linphone_address_get_username (addr);
	SalMediaDescription *md=sal_media_description_new();

	md->nstreams=1;
	strncpy(md->addr,call->localip,sizeof(md->addr));
	strncpy(md->username,username,sizeof(md->username));
	md->bandwidth=linphone_core_get_download_bandwidth(lc);
	/*set audio capabilities */
	strncpy(md->streams[0].addr,call->localip,sizeof(md->streams[0].addr));
	md->streams[0].port=call->audio_port;
	md->streams[0].proto=SalProtoRtpAvp;
	md->streams[0].type=SalAudio;
	md->streams[0].ptime=lc->net_conf.down_ptime;
	l=make_codec_list(lc,lc->codecs_conf.audio_codecs,call->params.audio_bw);
	pt=payload_type_clone(rtp_profile_get_payload_from_mime(&av_profile,"telephone-event"));
	l=ms_list_append(l,pt);
	md->streams[0].payloads=l;

	if (lc->dw_audio_bw>0)
		md->streams[0].bandwidth=lc->dw_audio_bw;

	if (call->params.has_video){
		md->nstreams++;
		md->streams[1].port=call->video_port;
		md->streams[1].proto=SalProtoRtpAvp;
		md->streams[1].type=SalVideo;
		l=make_codec_list(lc,lc->codecs_conf.video_codecs,0);
		md->streams[1].payloads=l;
		if (lc->dw_video_bw)
			md->streams[1].bandwidth=lc->dw_video_bw;
	}
	linphone_address_destroy(addr);
	return md;
}

static int find_port_offset(LinphoneCore *lc){
	int offset;
	MSList *elem;
	int audio_port;
	bool_t already_used=FALSE;
	for(offset=0;offset<100;offset+=2){
		audio_port=linphone_core_get_audio_port (lc)+offset;
		already_used=FALSE;
		for(elem=lc->calls;elem!=NULL;elem=elem->next){
			LinphoneCall *call=(LinphoneCall*)elem->data;
			if (call->audio_port==audio_port) {
				already_used=TRUE;
				break;
			}
		}
		if (!already_used) break;
	}
	if (offset==100){
		ms_error("Could not find any free port !");
		return -1;
	}
	return offset;
}

static void linphone_call_init_common(LinphoneCall *call, LinphoneAddress *from, LinphoneAddress *to){
	int port_offset;
	call->refcnt=1;
	call->state=LinphoneCallIdle;
	call->start_time=time(NULL);
	call->media_start_time=0;
	call->log=linphone_call_log_new(call, from, to);
	linphone_core_notify_all_friends(call->core,LinphoneStatusOnThePhone);
	port_offset=find_port_offset (call->core);
	if (port_offset==-1) return;
	call->audio_port=linphone_core_get_audio_port(call->core)+port_offset;
	call->video_port=linphone_core_get_video_port(call->core)+port_offset;
	call->audio_port = linphone_core_get_idel_udp_port(call->audio_port);
	call->video_port = linphone_core_get_idel_udp_port(call->video_port);

}

static void discover_mtu(LinphoneCore *lc, const char *remote){
	int mtu;
	if (lc->net_conf.mtu==0	){
		/*attempt to discover mtu*/
		mtu=ms_discover_mtu(remote);
		if (mtu>0){
			ms_set_mtu(mtu);
			ms_message("Discovered mtu is %i, RTP payload max size is %i",
				mtu, ms_get_payload_max_size());
		}
	}
}

LinphoneCall * linphone_call_new_outgoing(struct _LinphoneCore *lc, LinphoneAddress *from, LinphoneAddress *to, const LinphoneCallParams *params)
{
	LinphoneCall *call=ms_new0(LinphoneCall,1);
	call->dir=LinphoneCallOutgoing;
	call->op=sal_op_new(lc->sal);
	sal_op_set_user_pointer(call->op,call);
	call->core=lc;
	linphone_core_get_local_ip(lc,linphone_address_get_domain(to),call->localip);
	linphone_call_init_common(call,from,to);
	call->params=*params;
	call->localdesc=create_local_media_description (lc,call);
	call->camera_active=params->has_video;
	call->conf_mode = params->conf_mode;
	if (linphone_core_get_firewall_policy(call->core)==LinphonePolicyUseStun)
		linphone_core_run_stun_tests(call->core,call);
	//discover_mtu(lc,linphone_address_get_domain (to));
	if (params->referer){
		sal_call_set_referer (call->op,params->referer->op);
	}
	return call;
}

LinphoneCall * linphone_call_new_incoming(LinphoneCore *lc, LinphoneAddress *from, LinphoneAddress *to, SalOp *op){
	LinphoneCall *call=ms_new0(LinphoneCall,1);
	char *to_str;
	char *from_str;

	call->dir=LinphoneCallIncoming;
	sal_op_set_user_pointer(op,call);
	call->op=op;
	call->core=lc;

	if (lc->sip_conf.ping_with_options){
		/*the following sends an option request back to the caller so that
		we get a chance to discover our nat'd address before answering.*/
		call->ping_op=sal_op_new(lc->sal);
		to_str=linphone_address_as_string(to);
		from_str=linphone_address_as_string(from);
		sal_op_set_route(call->ping_op,sal_op_get_network_origin(call->op));
		sal_op_set_user_pointer(call->ping_op,call);
		sal_ping(call->ping_op,to_str,from_str);
		ms_free(to_str);
		ms_free(from_str);
	}

	linphone_address_clean(from);
	linphone_core_get_local_ip(lc,linphone_address_get_domain(from),call->localip);
	linphone_call_init_common(call, from, to);
	call->params.has_video=linphone_core_video_enabled(lc);
	call->localdesc=create_local_media_description (lc,call);
	call->camera_active=call->params.has_video;
#ifdef ENABLED_MCU_MEDIA_SERVER
	call->conf_mode = linphone_core_get_mcu_mode(lc);
#endif // ENABLED_MCU_MEDIA_SERVER
	if (linphone_core_get_firewall_policy(call->core)==LinphonePolicyUseStun)
		linphone_core_run_stun_tests(call->core,call);
	//discover_mtu(lc,linphone_address_get_domain(from));
	return call;
}

/* this function is called internally to get rid of a call.
It performs the following tasks:
- remove the call from the internal list of calls
- unref the LinphoneCall object
- update the call logs accordingly
*/

static void linphone_call_set_terminated(LinphoneCall *call){
	LinphoneCallStatus status=LinphoneCallAborted;
	LinphoneCore *lc=call->core;

	linphone_core_update_allocated_audio_bandwidth(lc);
	if (call->state==LinphoneCallEnd){
		if (call->reason==LinphoneReasonDeclined){
			if(call->dir == LinphoneCallIncoming)
				status=LinphoneCallMissed;
			else
				status=LinphoneCallDeclined;
		} 
		else status=LinphoneCallSuccess;

	}
	linphone_call_log_completed(call->log,call, status);

	if (call == lc->current_call){
		ms_message("Resetting the current call");
		lc->current_call=NULL;
	}

	if (linphone_core_del_call(lc,call) != 0){
		ms_error("Could not remove the call from the list !!!");
	}

	if (ms_list_size(lc->calls)==0)
		linphone_core_notify_all_friends(lc,lc->presence_mode);

	if (call->op!=NULL) {
		/* so that we cannot have anymore upcalls for SAL
		concerning this call*/
		sal_op_release(call->op);
		call->op=NULL;
	}
	linphone_call_unref(call);
}

const char *linphone_call_state_to_string(LinphoneCallState cs){
	switch (cs){
		case LinphoneCallIdle:
			return "空闲";
		case LinphoneCallIncomingReceived:
			return "新来电";
		case LinphoneCallOutgoingInit:
			return "初始化外呼";
		case LinphoneCallOutgoingProgress:
			return "外呼中";
		case LinphoneCallOutgoingRinging:
			return "外呼振铃";
		case LinphoneCallOutgoingEarlyMedia:
			return "外呼早期媒体";
		case LinphoneCallConnected:
			return "已连接";
		case LinphoneCallStreamsRunning:
			return "正在通话";
		case LinphoneCallVideoRecording:
			return "视频录制";
		case LinphoneCallInConferencing:
			return "会议中";
		case LinphoneCallPausing:
			return "呼叫暂停";
		case LinphoneCallPaused:
			return "保持呼叫";
		case LinphoneCallResuming:
			return "呼叫恢复";
		case LinphoneCallRefered:
			return "转接";
		case LinphoneCallError:
			return "错误";
		case LinphoneCallEnd:
			return "结束";
		case LinphoneCallPausedByRemote:
			return "远端保持";
		case LinphoneCallUpdatedByRemote:
			return "远端刷新";
		case LinphoneCallIncomingEarlyMedia:
			return "来电早期媒体";
		case LinphoneCallUpdated:
			return "更新状态";
	}
	return "undefined state";
}

void linphone_call_set_state(LinphoneCall *call, LinphoneCallState cstate, const char *message){
	LinphoneCore *lc=call->core;
	bool_t finalize_call=FALSE;
	if (call->state!=cstate){
		ms_message("Call %p: moving from state %s to %s",call,linphone_call_state_to_string(call->state),
			linphone_call_state_to_string(cstate));
		if (cstate!=LinphoneCallRefered){
			/*LinphoneCallRefered is rather an event, not a state.
			Indeed it does not change the state of the call (still paused or running)*/
			call->state=cstate;
		}
		if (cstate==LinphoneCallEnd || cstate==LinphoneCallError){
			finalize_call=TRUE;
			linphone_call_ref(call);
			linphone_call_set_terminated (call);
		}
		if (lc->vtable.call_state_changed)
			lc->vtable.call_state_changed(lc,call,cstate,message,call->last_error_code);
		if (finalize_call)
			linphone_call_unref(call);
	}
}

static void linphone_call_destroy(LinphoneCall *obj)
{
	if (obj->op!=NULL) {
		sal_op_release(obj->op);
		obj->op=NULL;
	}
	if (obj->resultdesc!=NULL) {
		sal_media_description_unref(obj->resultdesc);
		obj->resultdesc=NULL;
	}
	if (obj->localdesc!=NULL) {
		sal_media_description_unref(obj->localdesc);
		obj->localdesc=NULL;
	}
	if (obj->ping_op) {
		sal_op_release(obj->ping_op);
	}
	if (obj->refer_to){
		ms_free(obj->refer_to);
	}
	if(obj->os){
		video_recoder_destory(obj->os);
		obj->os=NULL;
	}
	ms_free(obj);
}

/**
* @addtogroup call_control
* @{
**/

/**
* Increments the call 's reference count.
* An application that wishes to retain a pointer to call object
* must use this function to unsure the pointer remains
* valid. Once the application no more needs this pointer,
* it must call linphone_call_unref().
**/
void linphone_call_ref(LinphoneCall *obj){
	obj->refcnt++;
}

/**
* Decrements the call object reference count.
* See linphone_call_ref().
**/
void linphone_call_unref(LinphoneCall *obj){
	obj->refcnt--;
	if (obj->refcnt==0){
		linphone_call_destroy(obj);
	}
}

/**
* Returns current parameters associated to the call.
**/
const LinphoneCallParams * linphone_call_get_current_params(const LinphoneCall *call){
	return &call->params;
}

/**
* Returns the remote address associated to this call
*
**/
const LinphoneAddress * linphone_call_get_remote_address(const LinphoneCall *call){
	return call->dir==LinphoneCallIncoming ? call->log->from : call->log->to;
}

/**
* Returns the remote address associated to this call as a string.
*
* The result string must be freed by user using ms_free().
**/
char *linphone_call_get_remote_address_as_string(const LinphoneCall *call){
	return linphone_address_as_string(linphone_call_get_remote_address(call));
}

/**
* Retrieves the call's current state.
**/
LinphoneCallState linphone_call_get_state(const LinphoneCall *call){
	return call->state;
}

/**
* Returns the reason for a call termination (either error or normal termination)
**/
LinphoneReason linphone_call_get_reason(const LinphoneCall *call){
	return call->reason;
}

/**
* Get the user_pointer in the LinphoneCall
*
* @ingroup call_control
*
* return user_pointer an opaque user pointer that can be retrieved at any time
**/
void *linphone_call_get_user_pointer(LinphoneCall *call)
{
	return call->user_pointer;
}

/**
* Set the user_pointer in the LinphoneCall
*
* @ingroup call_control
*
* the user_pointer is an opaque user pointer that can be retrieved at any time in the LinphoneCall
**/
void linphone_call_set_user_pointer(LinphoneCall *call, void *user_pointer)
{
	call->user_pointer = user_pointer;
}

/**
* Returns the call log associated to this call.
**/
LinphoneCallLog *linphone_call_get_call_log(const LinphoneCall *call){
	return call->log;
}

/**
* Returns the refer-to uri (if the call was transfered).
**/
const char *linphone_call_get_refer_to(const LinphoneCall *call){
	return call->refer_to;
}

/**
* Returns direction of the call (incoming or outgoing).
**/
LinphoneCallDir linphone_call_get_dir(const LinphoneCall *call){
	return call->log->dir;
}

/**
* Returns the far end's user agent description string, if available.
**/
const char *linphone_call_get_remote_user_agent(LinphoneCall *call){
	if (call->op){
		return sal_op_get_remote_ua (call->op);
	}
	return NULL;
}

/**
* Returns true if this calls has received a transfer that has not been
* executed yet.
* Pending transfers are executed when this call is being paused or closed,
* locally or by remote endpoint.
* If the call is already paused while receiving the transfer request, the 
* transfer immediately occurs.
**/
bool_t linphone_call_has_transfer_pending(const LinphoneCall *call){
	return call->refer_pending;
}

/**
* Returns call's duration in seconds.
**/
int linphone_call_get_duration(const LinphoneCall *call){
	if (call->media_start_time==0) return 0;
	return time(NULL)-call->media_start_time;
}

/**
* Indicate whether camera input should be sent to remote end.
**/
void linphone_call_enable_camera (LinphoneCall *call, bool_t enable){
#ifdef VIDEO_ENABLED
	if (call->videostream!=NULL && call->videostream->ticker!=NULL){
		LinphoneCore *lc=call->core;
		MSWebCam *nowebcam=get_nowebcam_device();
		if (call->camera_active!=enable && lc->video_conf.device!=nowebcam){
			video_stream_change_camera(call->videostream,
				enable ? lc->video_conf.device : nowebcam);
		}
	}
	call->camera_active=enable;
#endif
}

/**
* Take a photo of currently received video and write it into a jpeg file.
**/
int linphone_call_take_video_snapshot(LinphoneCall *call, const char *file){
#ifdef VIDEO_ENABLED
	if (call->videostream!=NULL && call->videostream->jpegwriter!=NULL){
		return ms_filter_call_method(call->videostream->jpegwriter,MS_JPEG_WRITER_TAKE_SNAPSHOT,(void*)file);
	}
	ms_warning("Cannot take snapshot: no currently running video stream on this call.");
	return -1;
#endif
	return -1;
}

/**
*
**/
bool_t linphone_call_camera_enabled (const LinphoneCall *call){
	return call->camera_active;
}

/**
* 
**/
void linphone_call_params_enable_video(LinphoneCallParams *cp, bool_t enabled){
	cp->has_video=enabled;
}

/**
*
**/
bool_t linphone_call_params_video_enabled(const LinphoneCallParams *cp){
	return cp->has_video;
}

/**
* Enable sending of real early media (during outgoing calls).
**/
void linphone_call_params_enable_early_media_sending(LinphoneCallParams *cp, bool_t enabled){
	cp->real_early_media=enabled;
}

bool_t linphone_call_params_early_media_sending_enabled(const LinphoneCallParams *cp){
	return cp->real_early_media;
}

/**
* Refine bandwidth settings for this call by setting a bandwidth limit for audio streams.
* As a consequence, codecs whose bitrates are not compatible with this limit won't be used.
**/
void linphone_call_params_set_audio_bandwidth_limit(LinphoneCallParams *cp, int bandwidth){
	cp->audio_bw=bandwidth;
}

/**
*
**/
LinphoneCallParams * linphone_call_params_copy(const LinphoneCallParams *cp){
	LinphoneCallParams *ncp=ms_new0(LinphoneCallParams,1);
	memcpy(ncp,cp,sizeof(LinphoneCallParams));
	return ncp;
}

/**
*
**/
void linphone_call_params_destroy(LinphoneCallParams *p){
	ms_free(p);
}

/**
* @}
**/


#ifdef TEST_EXT_RENDERER
static void rendercb(void *data, const MSPicture *local, const MSPicture *remote){
	ms_message("rendercb, local buffer=%p, remote buffer=%p",
		local ? local->planes[0] : NULL, remote? remote->planes[0] : NULL);
}
#endif

void linphone_call_init_media_streams(LinphoneCall *call){
	LinphoneCore *lc=call->core;
	SalMediaDescription *md=call->localdesc;
	AudioStream *audiostream;

	call->audiostream=audiostream=audio_stream_new(md->streams[0].port,linphone_core_ipv6_enabled(lc));
	if (linphone_core_echo_limiter_enabled(lc)){
		const char *type=lp_config_get_string(lc->config,"sound","el_type","mic");
		if (strcasecmp(type,"mic")==0)
			audio_stream_enable_echo_limiter(audiostream,ELControlMic);
		else if (strcasecmp(type,"full")==0)
			audio_stream_enable_echo_limiter(audiostream,ELControlFull);
	}
	audio_stream_enable_gain_control(audiostream,TRUE);
	if (linphone_core_echo_cancellation_enabled(lc)){
		int len,delay,framesize;
		len=lp_config_get_int(lc->config,"sound","ec_tail_len",0);
		delay=lp_config_get_int(lc->config,"sound","ec_delay",0);
		framesize=lp_config_get_int(lc->config,"sound","ec_framesize",0);
		audio_stream_set_echo_canceller_params(audiostream,len,delay,framesize);
	}
	audio_stream_enable_automatic_gain_control(audiostream,linphone_core_agc_enabled(lc));
	{
		int enabled=lp_config_get_int(lc->config,"sound","noisegate",0);
		audio_stream_enable_noise_gate(audiostream,enabled);
	}
	if (lc->a_rtp)
		rtp_session_set_transports(audiostream->session,lc->a_rtp,lc->a_rtcp);

#ifdef VIDEO_ENABLED
	if ((lc->video_conf.display || lc->video_conf.capture) && md->streams[1].port>0){
		call->videostream=video_stream_new(md->streams[1].port,linphone_core_ipv6_enabled(lc));
		if( lc->video_conf.displaytype != NULL)
			video_stream_set_display_filter_name(call->videostream,lc->video_conf.displaytype);
#ifdef TEST_EXT_RENDERER
		video_stream_set_render_callback(call->videostream,rendercb,NULL);
#endif
		if (lc->v_rtp && call->videostream)
			rtp_session_set_transports(call->videostream->session,lc->v_rtp,lc->v_rtcp);
	}
#else
	call->videostream=NULL;
#endif
}


static int dtmf_tab[16]={'0','1','2','3','4','5','6','7','8','9','*','#','A','B','C','D'};

static void linphone_core_dtmf_received(RtpSession* s, int dtmf, void* user_data){
	dtmf_user_data* ptr = (dtmf_user_data*)user_data;
	LinphoneCall *call = ptr->call;
	LinphoneCore *lc = ptr->lc;
	if (dtmf<0 || dtmf>15){
		ms_warning("Bad dtmf value %i",dtmf);
		return;
	}
	if (lc->vtable.dtmf_received != NULL && call != NULL)
		lc->vtable.dtmf_received(lc, call, dtmf_tab[dtmf]);
}

#ifndef ENABLED_MCU_MEDIA_SERVER
static void parametrize_equalizer(LinphoneCore *lc, AudioStream *st){
	if (st->equalizer){
		MSFilter *f=st->equalizer;
		int enabled=lp_config_get_int(lc->config,"sound","eq_active",0);
		const char *gains=lp_config_get_string(lc->config,"sound","eq_gains",NULL);
		ms_filter_call_method(f,MS_EQUALIZER_SET_ACTIVE,&enabled);
		if (enabled){
			if (gains){
				do{
					int bytes;
					MSEqualizerGain g;
					if (sscanf(gains,"%f:%f:%f %n",&g.frequency,&g.gain,&g.width,&bytes)==3){
						ms_message("Read equalizer gains: %f(~%f) --> %f",g.frequency,g.width,g.gain);
						ms_filter_call_method(f,MS_EQUALIZER_SET_GAIN,&g);
						gains+=bytes;
					}else break;
				}while(1);
			}
		}
	}
}
#endif // ENABLED_MCU_MEDIA_SERVER

static void post_configure_audio_streams(LinphoneCall*call){
	AudioStream *st=call->audiostream;
	LinphoneCore *lc=call->core;
	float mic_gain=lp_config_get_float(lc->config,"sound","mic_gain",1);
	float thres = 0;
	float recv_gain;
	float ng_thres=lp_config_get_float(lc->config,"sound","ng_thres",0.05);
	float ng_floorgain=lp_config_get_float(lc->config,"sound","ng_floorgain",0);
	int dc_removal=lp_config_get_int(lc->config,"sound","dc_removal",0);

	if (!call->audio_muted)
		audio_stream_set_mic_gain(st,mic_gain);
	else 
		audio_stream_set_mic_gain(st,0);

	recv_gain = lc->sound_conf.soft_play_lev;
	if (recv_gain != 0) {
		linphone_core_set_playback_gain_db (lc,recv_gain);
	}
#ifndef ENABLED_MCU_MEDIA_SERVER
	if (st->volsend){
		ms_filter_call_method(st->volsend,MS_VOLUME_REMOVE_DC,&dc_removal);
	}
	if (linphone_core_echo_limiter_enabled(lc)){
		float speed=lp_config_get_float(lc->config,"sound","el_speed",-1);
		float force=lp_config_get_float(lc->config,"sound","el_force",-1);
		int sustain=lp_config_get_int(lc->config,"sound","el_sustain",-1);
		MSFilter *f=NULL;
		thres=lp_config_get_float(lc->config,"sound","el_thres",-1);
		if (st->el_type!=ELInactive){
			f=st->volsend;
			if (speed==-1) speed=0.03;
			if (force==-1) force=25;
			ms_filter_call_method(f,MS_VOLUME_SET_EA_SPEED,&speed);
			ms_filter_call_method(f,MS_VOLUME_SET_EA_FORCE,&force);
			if (thres!=-1)
				ms_filter_call_method(f,MS_VOLUME_SET_EA_THRESHOLD,&thres);
			if (sustain!=-1)
				ms_filter_call_method(f,MS_VOLUME_SET_EA_SUSTAIN,&sustain);
		}
	}

	if (st->volsend){
		ms_filter_call_method(st->volsend,MS_VOLUME_SET_NOISE_GATE_THRESHOLD,&ng_thres);
		ms_filter_call_method(st->volsend,MS_VOLUME_SET_NOISE_GATE_FLOORGAIN,&ng_floorgain);
	}
	if (st->volrecv){
		/* parameters for a limited noise-gate effect, using echo limiter threshold */
		float floorgain = 1/mic_gain;
		ms_filter_call_method(st->volrecv,MS_VOLUME_SET_NOISE_GATE_THRESHOLD,&thres);
		ms_filter_call_method(st->volrecv,MS_VOLUME_SET_NOISE_GATE_FLOORGAIN,&floorgain);
	}
	parametrize_equalizer(lc,st);
#endif // ENABLED_MCU_MEDIA_SERVER
	if (lc->vtable.dtmf_received!=NULL){
		if(call->user_data==NULL)
			call->user_data= ms_new0(dtmf_user_data,1);

		call->user_data->call = call;
		call->user_data->lc = lc;

		/* replace by our default action*/
		audio_stream_play_received_dtmfs(call->audiostream,FALSE);
		rtp_session_signal_connect(call->audiostream->session,"telephone-event",(RtpCallback)linphone_core_dtmf_received,(unsigned long)call->user_data);
	}
}




static RtpProfile *make_profile(LinphoneCore *lc, const SalMediaDescription *md, const SalStreamDescription *desc, int *used_pt){
	int bw;
	const MSList *elem;
	RtpProfile *prof=rtp_profile_new("Call profile");
	bool_t first=TRUE;
	int remote_bw=0;
	*used_pt=-1;

	for(elem=desc->payloads;elem!=NULL;elem=elem->next){
		PayloadType *pt=(PayloadType*)elem->data;
		int number;

		if (first) {
			if (desc->type==SalAudio){
				linphone_core_update_allocated_audio_bandwidth_in_call(lc,pt);
			}
			*used_pt=payload_type_get_number(pt);
			first=FALSE;
		}
		if (desc->bandwidth>0) remote_bw=desc->bandwidth;
		else if (md->bandwidth>0) {
			/*case where b=AS is given globally, not per stream*/
			remote_bw=md->bandwidth;
			if (desc->type==SalVideo){
				remote_bw-=lc->audio_bw;
			}
		}

		if (desc->type==SalAudio){			
			bw=get_min_bandwidth(lc->up_audio_bw,remote_bw);
		}else bw=get_min_bandwidth(lc->up_video_bw,remote_bw);
		if (bw>0) pt->normal_bitrate=bw*1000;
		else if (desc->type==SalAudio){
			pt->normal_bitrate=-1;
		}
		if (desc->ptime>0){
			char tmp[40];
			snprintf(tmp,sizeof(tmp),"ptime=%i",desc->ptime);
			payload_type_append_send_fmtp(pt,tmp);
		}
		number=payload_type_get_number(pt);
		if (rtp_profile_get_payload(prof,number)!=NULL){
			ms_warning("A payload type with number %i already exists in profile !",number);
		}else
			rtp_profile_set_payload(prof,number,pt);
	}
	return prof;
}


static void setup_ring_player(LinphoneCore *lc, LinphoneCall *call){
	int pause_time=3000;
	audio_stream_play(call->audiostream,lc->sound_conf.ringback_tone);
	ms_filter_call_method(call->audiostream->soundread,MS_FILE_PLAYER_LOOP,&pause_time);
}


#include "msudt.h"

void linphone_call_start_media_streams(LinphoneCall *call, bool_t all_inputs_muted, bool_t send_ringbacktone){
	LinphoneCore *lc=call->core;
	LinphoneAddress *me=linphone_core_get_primary_contact_parsed(lc);
	const char *tool="QfishPhone-" LINPHONE_VERSION;
	char *cname;
	int jitt_comp;
	int used_pt=-1;

	if(call->audiostream == NULL)
	{
		ms_fatal("start_media_stream() called without prior init !");
		return;
	}
	/* adjust rtp jitter compensation. It must be at least the latency of the sound card */
	jitt_comp=MAX(lc->sound_conf.latency,lc->rtp_conf.audio_jitt_comp);

	if (call->media_start_time==0) call->media_start_time=time(NULL);

	cname=linphone_address_as_string_uri_only(me);
	{
		const SalStreamDescription *stream=sal_media_description_find_stream(call->resultdesc,
			SalProtoRtpAvp,SalAudio);
		if (stream && stream->dir!=SalStreamInactive){

			const char *playfile=lc->play_file;
			const char *recfile=lc->rec_file;

			MSSndCard *playcard=lc->sound_conf.lsd_card ? 
				lc->sound_conf.lsd_card : lc->sound_conf.play_sndcard;

			MSSndCard *captcard=lc->sound_conf.capt_sndcard;


			call->audio_profile=make_profile(lc,call->resultdesc,stream,&used_pt);
			if (used_pt!=-1){
				if (playcard==NULL) {
					ms_warning("No card defined for playback !");
				}
				if (captcard==NULL) {
					ms_warning("No card defined for capture !");
				}
				/*Replace soundcard filters by inactive file players or recorders
				when placed in recvonly or sendonly mode*/
				if (stream->port==0 || stream->dir==SalStreamRecvOnly){
					captcard=NULL;
					playfile=NULL;
				}else if (stream->dir==SalStreamSendOnly){
					playcard=NULL;
					captcard=NULL;
					recfile=NULL;
					/*playfile=NULL;*/
				}
				if (send_ringbacktone){
					captcard=NULL;
					playfile=NULL;/* it is setup later*/
				}
				/*if playfile are supplied don't use soundcards*/
				if (lc->use_files) {
					captcard=NULL;
					playcard=NULL;
				}

				//call->audiostream->record_enabled =call->enable_audio_record;
				//call->audiostream->enable_udt = call->enabled_udt;
				audio_stream_start_full(
					call->audiostream,
					call->audio_profile,
					stream->addr[0]!='\0' ? stream->addr : call->resultdesc->addr,
					stream->port,
					stream->port+1,
					used_pt,
					jitt_comp,
					playfile,
					recfile,
					playcard,
					captcard,
					captcard==NULL ? FALSE : linphone_core_echo_cancellation_enabled(lc));
				post_configure_audio_streams(call);
				if (all_inputs_muted && !send_ringbacktone){
					audio_stream_set_mic_gain(call->audiostream,0);
				}
				if (send_ringbacktone){
					setup_ring_player(lc,call);
				}
				audio_stream_set_rtcp_information(call->audiostream, cname, tool);
			}else ms_warning("No audio stream accepted ?");
		}
	}
#ifdef VIDEO_ENABLED
	{
		const SalStreamDescription *stream=sal_media_description_find_stream(call->resultdesc,
			SalProtoRtpAvp,SalVideo);
		used_pt=-1;
		/* shutdown preview */
		if (lc->previewstream!=NULL) {
			video_preview_stop(lc->previewstream);
			lc->previewstream=NULL;
		}



		if (stream && stream->dir!=SalStreamInactive) {
			const char *addr=stream->addr[0]!='\0' ? stream->addr : call->resultdesc->addr;
			call->video_profile=make_profile(lc,call->resultdesc,stream,&used_pt);
			if (used_pt!=-1){
				VideoStreamDir dir=VideoStreamSendRecv;
				MSWebCam *cam=lc->video_conf.device;
				bool_t is_inactive=FALSE;

				call->params.has_video=TRUE;

				video_stream_set_sent_video_size(call->videostream,linphone_core_get_preferred_video_size(lc));
				video_stream_enable_self_view(call->videostream,lc->video_conf.selfview);
				if (lc->video_window_id!=0)
					video_stream_set_native_window_id(call->videostream,lc->video_window_id);
				if (lc->preview_window_id!=0)
					video_stream_set_native_preview_window_id (call->videostream,lc->preview_window_id);
				video_stream_use_preview_video_window (call->videostream,lc->use_preview_window);

				if (stream->dir==SalStreamSendOnly && lc->video_conf.capture ){
					cam=get_nowebcam_device();
					dir=VideoStreamSendOnly;
				}else if (stream->dir==SalStreamRecvOnly && lc->video_conf.display ){
					dir=VideoStreamRecvOnly;
				}else if (stream->dir==SalStreamSendRecv){
					if (lc->video_conf.display && lc->video_conf.capture)
						dir=VideoStreamSendRecv;
					else if (lc->video_conf.display)
						dir=VideoStreamRecvOnly;
					else
						dir=VideoStreamSendOnly;
				}else{
					ms_warning("video stream is inactive.");
					/*either inactive or incompatible with local capabilities*/
					is_inactive=TRUE;
				}
				if (call->camera_active==FALSE || all_inputs_muted){
					cam=get_nowebcam_device();
				}

#ifdef ENABLED_MCU_MEDIA_SERVER
				if(call->conf_mode)
					video_stream_enable_conference_mode(call->videostream,call->conf_mode);
#endif // ENABLED_MCU_MEDIA_SERVER

				if (!is_inactive){
					video_stream_set_direction (call->videostream, dir);
#ifdef UDT_ENABLED
					call->videostream->enable_udt = call->enabled_udt;
#endif // UDT_ENABLED
					video_stream_start(call->videostream,
						call->video_profile, addr, stream->port,
						stream->port+1,
						used_pt, jitt_comp, cam);
					video_stream_set_rtcp_information(call->videostream, cname,tool);
				}
			}else ms_warning("No video stream accepted.");
		}else{
			ms_warning("No valid video stream defined.");
		}
	}
#endif
	call->all_muted=all_inputs_muted;
	call->playing_ringbacktone=send_ringbacktone;
	call->up_bw=linphone_core_get_upload_bandwidth(lc);

	goto end;
end:
	ms_free(cname);
	linphone_address_destroy(me);
}

static void linphone_call_log_fill_stats(LinphoneCallLog *log, AudioStream *st){
	audio_stream_get_local_rtp_stats (st,&log->local_stats);
}

void linphone_call_stop_media_streams(LinphoneCall *call){
	if (call->audiostream!=NULL) {
		linphone_call_log_fill_stats (call->log,call->audiostream);
		audio_stream_stop(call->audiostream);
		call->audiostream=NULL;
	}
#ifdef VIDEO_ENABLED
	if (call->videostream!=NULL){
		video_stream_stop(call->videostream);
		call->videostream=NULL;
	}
	if(call->os)
		video_recoder_stop(call->os);

#endif
	if (call->audio_profile){
		rtp_profile_clear_all(call->audio_profile);
		rtp_profile_destroy(call->audio_profile);
		call->audio_profile=NULL;
	}
	if (call->video_profile){
		rtp_profile_clear_all(call->video_profile);
		rtp_profile_destroy(call->video_profile);
		call->video_profile=NULL;
	}
	if(call->user_data!=NULL){
		ms_free(call->user_data);
		call->user_data=NULL;
	}
}


void linphone_call_video_vfu_request(LinphoneCall *call){
#ifdef VIDEO_ENABLED
	if (call==NULL){
		ms_warning("VFU request but no call !");
		return ;
	}
	if (call->videostream)
		video_stream_send_vfu(call->videostream);
#endif
}

/*播放视频文件*/
int linphone_call_play_video_file(LinphoneCall *call,const char *filename)
{
	/*播放流程

	1, 创建VideoPlayer
	2, 设置文件名,根据Video Call 设置视频尺寸
	3, 创建虚拟sndcard,webcam
	4, 重启audio_stream,video_stream 并连接新的 sndcard,webcam
	5, 启动文件读取，输出音视频流
	6, 呼叫停止,销毁VideoPlayer
	*/
	call->is = file_stream_new();
}

/*录制视频呼叫*/
int linphone_call_record_video_file(LinphoneCall *call,const char *filename,MSVideoSize vsize, int bit_rate)
{
	/* 录制流程

	1, 创建VideoRecorder
	2, 设置文件名,根据Video Call设置视频尺寸,文件编码率
	3, 创建虚拟sndcard,output接口
	4, 重启audio_stream,video_stream 并连接新的 sndcard,output接口
	5, 启动文件读取，输出音视频流
	6, 继续/暂停录制器
	7, 呼叫停止,销毁VideoRecorder,保存文件

	音频filter结构
	MSRtpRecv -->MSGsmDec -->MSTee --> MSSndWriter
	|
	|--- -->MSAudioRecoder--------
	|
	| 
	|-----------> VideoRecoder    
	视频filter结构                                          |
	MSRtpRecv -->MSH264Dec -->MSTee --> MSDrawDibDisplay    |
	|                            |
	|--- -->MSJpegWriter         |
	|                            |
	|--- -->MSVideoRecoder -------
	*/

	int nchannels=1;
	int rate=8000;
	int audio_bit_rate=32;
	int video_bit_rate=bit_rate-audio_bit_rate;
	float fps = 15.0;
	struct _AudioStream *audiostream=NULL;
	struct _VideoStream *videostream=NULL;

	if(call==NULL || call->videostream==NULL) return -1;

	audiostream=call->audiostream;
	videostream=call->videostream;

	linphone_call_stop_media_streams(call);

	if(call->os==NULL) call->os = video_recoder_new();

	video_recoder_init(call->os);

	//ms_filter_call_method(videostream->output,MS_FILTER_GET_VIDEO_SIZE,&vsize);

	/*设置参数*/
	video_recoder_set_rate(call->os,rate);
//	video_recoder_set_nbchannels(call->os, nchannels);
	video_recoder_set_vsize(call->os, vsize);
	video_recoder_set_fps(call->os,fps);
	video_recoder_set_audio_bit_rate(call->os,audio_bit_rate);
	video_recoder_set_video_bit_rate(call->os,video_bit_rate);

	if(video_recoder_open_file(call->os,filename)==0){
		/*重启媒体流*/
		//停止audiostream,videostream
		linphone_call_init_media_streams(call);

		//连接新的output端口
		call->audiostream->tee2=ms_filter_new(MS_TEE_ID);
		call->audiostream->audio_record = video_recoder_create_audio_filter(call->os);
		if(call->videostream) call->videostream->video_record = video_recoder_create_video_filter(call->os);
		//启动audiostream,videostream
		linphone_call_start_media_streams(call,FALSE,FALSE);

		linphone_core_send_picture_fast_update(call);


		video_recoder_start(call->os);
		video_recoder_starting(call->os,TRUE);

		call->in_recording_video_file = TRUE;
	}else{
		video_recoder_destory(call->os);
		call->os=NULL;

		//停止audiostream,videostream
		linphone_call_init_media_streams(call);

		//启动audiostream,videostream
		linphone_call_start_media_streams(call,FALSE,FALSE);

		call->in_recording_video_file = FALSE;
		return -2;
	}

	return 0;
}


bool_t linphone_call_in_recording_video_file(LinphoneCall *call){
	return call->in_recording_video_file;
}


int linphone_core_call_restart_media_stream(LinphoneCore *lc, LinphoneCall *call)
{
	if (!call) -1;

	
	if (call->audiostream!=NULL) {
		linphone_call_log_fill_stats (call->log,call->audiostream);
		audio_stream_stop(call->audiostream);
		call->audiostream=NULL;
	}

	call->enable_audio_record =TRUE;

	linphone_call_init_media_streams(call);
	linphone_call_start_media_streams(call,FALSE,FALSE);

	return 0;
}


size_t my_strftime(char *s, size_t max, const char  *fmt,  const struct tm *tm){
#if !defined(_WIN32_WCE)
	return strftime(s, max, fmt, tm);
#else
	return 0;
	/*FIXME*/
#endif /*_WIN32_WCE*/
}


int linphone_core_call_start_recoding(LinphoneCore *lc, LinphoneCall *call,const char *path)
{

	char start_date[128];
	char *filename=NULL;
	char *record_path=NULL;
	struct tm loctime;

	if (!call || call->state != LinphoneCallStreamsRunning) -1;

	if(call->audiostream){

#ifdef WIN32
#if !defined(_WIN32_WCE)
		loctime=*localtime(&call->start_time);
#endif
#endif

		my_strftime(start_date,sizeof(start_date),"%Y%m%d%H%M%S",&loctime);

		filename = ms_strdup_printf("record_%s_%s.wav",
			(call->log->dir == LinphoneCallOutgoing)? linphone_address_get_username(call->log->to):linphone_address_get_username(call->log->from),
			start_date);

		strncpy(call->log->record_file,filename,sizeof(call->log->record_file));

		record_path = ms_strdup_printf("%s/%s",path,filename);

		ms_message("Start recording: %s",record_path);

		if(audio_stream_recored_file_enabled(call->audiostream))
			audio_stream_record_start(call->audiostream,record_path);
		else{
			linphone_core_call_restart_media_stream(lc,call);
			audio_stream_record_start(call->audiostream,record_path);
		}


	}

	if(filename) ms_free(filename);
	if(record_path) ms_free(record_path);

	return 0;
}

int linphone_core_call_stop_recoding(LinphoneCore *lc, LinphoneCall *call)
{
	if (!call) -1;

	if(call->audiostream ){

	}
}