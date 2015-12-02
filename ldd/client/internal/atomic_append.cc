#include <map>
#include <glog/logging.h>
#include "ldd/util/time.h"
#include "ldd/protocol/server/store_error.h"
#include "atomic_append.h"
#include "../redo_simple_policy.h"

namespace ldd { 
namespace client {
namespace raw {

using namespace ldd::net;

AtomicAppendRequest::AtomicAppendRequest(ClientImpl* ct, const std::string& ns, 
    const std::string& key, 
	uint8_t u8position, const std::string& content, 
	uint64_t u64ttl, const CasCompletion& completion,
	const std::string& using_host, int using_port,
	RedoPolicy* rp) : 
	client_(ct),
	ns_(ns),
	key_(key),
	u8position_(u8position),
	content_(content),
	u64ttl_(u64ttl),
	cas_completion_(completion),
	using_host_(using_host),
	using_port_(using_port),
	rp_(rp){
}

bool AtomicAppendRequest::Init(Payload* request, ldd::util::TimeDiff* recv_timeout,
        ldd::util::TimeDiff* done_timeout){
    protocol::AtomicAppendMessage aam;
    aam.name_space_	 = ns_;
    aam.key_			 = key_;
    aam.u8position_	 = u8position_;
    aam.content_		 = content_;
    aam.u64ttl_		 = u64ttl_;

    *recv_timeout = util::TimeDiff::Milliseconds(client_->get_recv_timeout());
    *done_timeout = util::TimeDiff::Milliseconds(client_->get_done_timeout());

    net::Buffer  buf; 
        //构造请求，填充到request
    buf.Reset(aam.MaxSize());
    int len = aam.SerializeTo(buf.ptr());
    buf.ReSize(len);

    request->SetBody(buf);
    LOG(INFO)<<"AtomicAppendRequest::SerializeTo() create buffer len="<<len;
    return true;
}

bool AtomicAppendRequest::Recv(const Payload& response,
        ldd::util::TimeDiff* recv_timeout){
    
    *recv_timeout = util::TimeDiff::Milliseconds(client_->get_recv_timeout());

    if (!response_.ParseFrom(response.body().data(), response.body().size())) {
        cas_completion_(ldd::protocol::kNotValidPacket, "");
        LOG(ERROR) << "parse error from buff to response";
        return false;
    }

    LOG(INFO) << "AtomicAppendRequest::Recv() version=" << response_.s64Version_;
    cas_completion_(response_.s64Version_, response_.value_.ToString());

    return true;
}

void AtomicAppendRequest::Done(const Result& result){
    LOG(INFO)<<"AtomicAppendRequest::Done() result="<<result.code();    

    if (result.IsTimedOut()){
        LOG(INFO) << "error called";
        if (rp_->ShouldRety(util::Time::CurrentMilliseconds())){
            client_->Append(ns_, key_, u8position_, content_, u64ttl_, 
                cas_completion_, using_host_, using_port_, rp_);
            LOG(INFO)<<"AtomicAppendRequest::HandleError() retry ";
        }else{
            cas_completion_(ldd::protocol::kTimeout, "");
            LOG(ERROR) << "AtomicAppendRequest::HandleError() timeout called";
            delete rp_;
        }
    }else if (result.IsFailed()){
        cas_completion_(kFailed, "");
        delete rp_;
        LOG(ERROR) << "AtomicAppendRequest::Done() code="<<kFailed;
    }else if (result.IsCanceled()){
        cas_completion_(kCancel, "");
        delete rp_;
        LOG(ERROR) << "AtomicAppendRequest::Done() code="<<kCancel;
    }else if (result.IsOk()){
        delete rp_;
        if (result.code() == kOK){
            LOG(INFO)<<"AtomicAppendRequest::Done OK!!!";
        }else{
            cas_completion_(result.code(), "");
            LOG(ERROR) << "AtomicAppendRequest::Done() result="<<result.code();
        }
    }
}

   
} // namespace ldd {
} // namespace client {
} // namespace raw {
