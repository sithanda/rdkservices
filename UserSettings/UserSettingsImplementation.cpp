/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2024 Synamedia Ltd.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "UserSettingsImplementation.h"
#include <sys/prctl.h>
#include "UtilsJsonRpc.h"
#include <mutex>

namespace WPEFramework {
namespace Plugin {

SERVICE_REGISTRATION(UserSettingsImplementation, 1, 0);

UserSettingsImplementation::UserSettingsImplementation()
: _adminLock()
, m_engine(Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create())
, mClient(Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(m_engine)))
, m_remoteObject(nullptr)
, m_store_notification(*this)
, m_registeredEventHandlers(false)
{
    LOGINFO("Create UserSettingsImplementation Instance");

    UserSettingsImplementation::instance(this);

     if (!mClient.IsValid())
     {
         LOGWARN("Invalid mClient\n");
    }
    else
    {
        m_engine->Announcements(mClient->Announcement());

        LOGINFO("Connect the COM-RPC socket\n");
        mController = mClient->Open<PluginHost::IShell>(_T("org.rdk.PersistentStore"), ~0, 3000);

        if (mController)
        {
             m_remoteObject = mController->QueryInterface<Exchange::IStore2>();

             if(m_remoteObject)
             {
                 m_remoteObject->AddRef();
             }
        }
        else
        {
            LOGERR("Failed to create PersistentStore Controller\n");
        }

        registerEventHandlers();
    }
}

UserSettingsImplementation* UserSettingsImplementation::instance(UserSettingsImplementation *UserSettingsImpl)
{
   static UserSettingsImplementation *UserSettingsImpl_instance = nullptr;

   if (UserSettingsImpl != nullptr)
   {
      UserSettingsImpl_instance = UserSettingsImpl;
   }

   return(UserSettingsImpl_instance);
}

UserSettingsImplementation::~UserSettingsImplementation()
{
    if (mController)
    {
        mController->Release();
    }

    LOGINFO("Disconnect from the COM-RPC socket\n");
    // Disconnect from the COM-RPC socket
    mClient->Close(RPC::CommunicationTimeOut);
    if (mClient.IsValid())
    {
        mClient.Release();
    }

    if(m_engine.IsValid())
    {
        m_engine.Release();
    }

    if(m_remoteObject)
    {
        m_remoteObject->Release();
    }
    m_registeredEventHandlers = false;
}

void UserSettingsImplementation::registerEventHandlers()
{
    if(!m_registeredEventHandlers && m_remoteObject) {
        m_registeredEventHandlers = true;
        m_remoteObject->Register(&m_store_notification);
    }
}

/**
 * Register a notification callback
 */
uint32_t UserSettingsImplementation::Register(Exchange::IUserSettings::INotification *notification)
{
    _adminLock.Lock();

    // Make sure we can't register the same notification callback multiple times
    if (std::find(m_notification.begin(), m_notification.end(), notification) == m_notification.end())
    {
        LOGINFO("Register notification");
        m_notification.push_back(notification);
        notification->AddRef();
    }

    _adminLock.Unlock();

    return Core::ERROR_NONE;
}

/**
 * Unregister a notification callback
 */
uint32_t UserSettingsImplementation::Unregister(Exchange::IUserSettings::INotification *notification )
{
    _adminLock.Lock();

    // Make sure we can't unregister the same notification callback multiple times
    auto itr = std::find(m_notification.begin(), m_notification.end(), notification);
    if (itr != m_notification.end())
    {
        (*itr)->Release();
        LOGINFO("Unregister notification");
        m_notification.erase(itr);
    }

    _adminLock.Unlock();

    return Core::ERROR_NONE;
}

void UserSettingsImplementation::dispatchEvent(Event event, const JsonValue &params)
{
    Core::IWorkerPool::Instance().Submit(Job::Create(this, event, params));
}

void UserSettingsImplementation::Dispatch(Event event, const JsonValue params)
{
     _adminLock.Lock();

     std::list<Exchange::IUserSettings::INotification*>::iterator index(m_notification.begin());

     while (index != m_notification.end()) {
         switch(event) {
             case AUDIO_DESCRIPTION_CHANGED:     (*index)->OnAudioDescriptionChanged(params.Boolean()); break;
             case PREFERED_AUDIO_CHANGED:     (*index)->OnPreferredAudioLanguagesChanged(params.String()); break;
             case LOCALE_CHANGED:        (*index)->OnLocaleChanged(params.String()); break;
             case CAPTIONS_CHANGED:      (*index)->OnCaptionsChanged(params.Boolean()); break;
             case PREFERED_CAPTIONS_LANGUAGE_CHANGED:      (*index)->OnPreferredCaptionsLanguagesChanged(params.String()); break;
             case PREFERED_CLOSED_CAPTIONS_SERVICE_CHANGED:     (*index)->OnPreferredClosedCaptionServiceChanged(params.String()); break;
             default: break;
         }
         ++index;
     }

     _adminLock.Unlock();
}

void UserSettingsImplementation::ValueChanged(const Exchange::IStore2::ScopeType scope, const string& ns, const string& key, const string& value)
{
    LOGINFO("ns:%s key:%s value:%s", ns.c_str(), key.c_str(), value.c_str());

    if((ns.compare("UserSettings") == 0) && (key.compare("audioDescription") == 0))
    {
        dispatchEvent(AUDIO_DESCRIPTION_CHANGED, JsonValue((bool)(value.compare("true")==0)?true:false));
    }
    else if((ns.compare("UserSettings") == 0) && (key.compare("preferredAudioLanguages") == 0))
    {
        dispatchEvent(PREFERED_AUDIO_CHANGED, JsonValue((string)value));
    }
    else if((ns.compare("UserSettings") == 0) && (key.compare("locale") == 0))
    {
        dispatchEvent(LOCALE_CHANGED, JsonValue((string)value));
    }
    else if((ns.compare("UserSettings") == 0) && (key.compare("captions") == 0))
    {
        dispatchEvent(CAPTIONS_CHANGED, JsonValue((bool)(value.compare("true")==0)?true:false));
    }
    else if((ns.compare("UserSettings") == 0) && (key.compare("preferredCaptionsLanguages") == 0))
    {
        dispatchEvent(PREFERED_CAPTIONS_LANGUAGE_CHANGED, JsonValue((string)value));
    }
    else if((ns.compare("UserSettings") == 0) && (key.compare("preferredClosedCaptionsService") == 0))
    {
        dispatchEvent(PREFERED_CLOSED_CAPTIONS_SERVICE_CHANGED, JsonValue((string)value));
    }
    else
    {
        LOGERR("Not supported");
    }
}

uint32_t UserSettingsImplementation::SetAudioDescription(const bool &enabled)
{
    uint32_t status = Core::ERROR_GENERAL;

    LOGINFO("enabled: %d", enabled);

    _adminLock.Lock();

    status = m_remoteObject->SetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "audioDescription", (enabled)?"true":"false", 0);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::GetAudioDescription(bool &enabled) const
{
    uint32_t status = Core::ERROR_GENERAL;
    std::string value = "";
    uint32_t ttl = 0;

    _adminLock.Lock();

    status = m_remoteObject->GetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "audioDescription", value, ttl);

    if(Core::ERROR_NONE == status)
    {
        if (value.compare("true") == 0)
        {
            enabled = true;
        }
        else
        {
            enabled = false;
        }
    }

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::SetPreferredAudioLanguages(const string &preferredLanguages)
{
    uint32_t status = Core::ERROR_GENERAL;

    LOGINFO("preferredLanguages: %s", preferredLanguages.c_str());

    _adminLock.Lock();

    status = m_remoteObject->SetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "preferredAudioLanguages", preferredLanguages, 0);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::GetPreferredAudioLanguages(string &preferredLanguages) const
{
    uint32_t status = Core::ERROR_GENERAL;
    std::string value = "";
    uint32_t ttl = 0;

    _adminLock.Lock();

    status = m_remoteObject->GetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "preferredAudioLanguages", preferredLanguages, ttl);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::SetLocale(const string &locale)
{
    uint32_t status = Core::ERROR_GENERAL;

    LOGINFO("locale: %s", locale.c_str());

    _adminLock.Lock();

    status = m_remoteObject->SetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "locale", locale, 0);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::GetLocale(string &locale) const
{
    uint32_t status = Core::ERROR_GENERAL;
    std::string value = "";
    uint32_t ttl = 0;

    _adminLock.Lock();

    status = m_remoteObject->GetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "locale", locale, ttl);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::SetCaptions(const bool &enabled)
{
    uint32_t status = Core::ERROR_GENERAL;

    LOGINFO("enabled: %d", enabled);

    _adminLock.Lock();

    status = m_remoteObject->SetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "captions", (enabled)?"true":"false", 0);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::GetCaptions(bool &enabled) const
{
    uint32_t status = Core::ERROR_GENERAL;
    std::string value = "";
    uint32_t ttl = 0;

    _adminLock.Lock();

    status = m_remoteObject->GetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "captions", value, ttl);

    if(Core::ERROR_NONE == status)
    {
        if (value.compare("true") == 0)
        {
            enabled = true;
        }
        else
        {
            enabled = false;
        }
    }

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::SetPreferredCaptionsLanguages(const string &preferredLanguages)
{
    uint32_t status = Core::ERROR_GENERAL;

    LOGINFO("preferredLanguages: %s", preferredLanguages.c_str());

    _adminLock.Lock();

    status = m_remoteObject->SetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "preferredCaptionsLanguages", preferredLanguages, 0);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::GetPreferredCaptionsLanguages(string &preferredLanguages) const
{
    uint32_t status = Core::ERROR_GENERAL;
    std::string value = "";
    uint32_t ttl = 0;

    _adminLock.Lock();

    status = m_remoteObject->GetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "preferredCaptionsLanguages", preferredLanguages, ttl);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::SetPreferredClosedCaptionService(const std::string &service)
{
    uint32_t status = Core::ERROR_GENERAL;

    LOGINFO("service: %s", service.c_str());

    _adminLock.Lock();

    status = m_remoteObject->SetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "preferredClosedCaptionsService", service, 0);

    _adminLock.Unlock();

    return status;
}

uint32_t UserSettingsImplementation::GetPreferredClosedCaptionService(std::string &service) const
{
    uint32_t status = Core::ERROR_GENERAL;
    std::string value = "";
    uint32_t ttl = 0;

    _adminLock.Lock();

    status = m_remoteObject->GetValue(Exchange::IStore2::ScopeType::DEVICE, "UserSettings", "preferredClosedCaptionsService", service, ttl);

    _adminLock.Unlock();

    return status;
}
} // namespace Plugin
} // namespace WPEFramework
