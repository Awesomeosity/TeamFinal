// Copyright (c) 2018 fivefingergames.
// Copyright (c) 2019 Anthony Posselli

#include "TwitchAuthComponent.h"
#include "CoreMinimal.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

UTwitchAuthComponent::UTwitchAuthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

#pragma region Public

#pragma region Blueprint Interaction

FTwitchUser UTwitchAuthComponent::GetUser() const
{
    return m_TwitchUser;
}

void UTwitchAuthComponent::Authenticate(UWebBrowser* WebBrowser)
{
    if(m_AccessToken != "")
    {
        ExecuteGetUserRequest();
    }
    else
    {
        if(WebBrowser)
        {
            m_WebBrowser = WebBrowser;
            FString url = UTwitchApi::GetAuthenticationUrl(ClientId, Permissions, bForceVerify);
            m_WebBrowser->OnUrlChanged.AddDynamic(this, &UTwitchAuthComponent::HandleOnUrlChanged);
            m_WebBrowser->LoadURL(url);
        }
        else
        {
            FTwitchError error;
            error.status = 404;
            error.error = "Browser Widget not found";
            error.message = "Browser widget could not be found";
            m_LastError = error;

            LogError(m_LastError);
        }
    }
}

void UTwitchAuthComponent::IsUserSubscribedToChannel(const FString& ChannelName)
{
    m_TwitchRequest = ETwitchRequest::Subscription;
    ExecuteGetChannelRequest(ChannelName);
}

void UTwitchAuthComponent::IsUserFollowingChannel(const FString& ChannelName)
{
    m_TwitchRequest = ETwitchRequest::Following;
    ExecuteGetChannelRequest(ChannelName);
}

FString UTwitchAuthComponent::GetAccessToken() const
{
    return m_AccessToken;
}

void UTwitchAuthComponent::SetAccessToken(const FString& AccessToken)
{
    m_AccessToken = AccessToken;
}

void UTwitchAuthComponent::ClearAccessToken()
{
    m_AccessToken = "";
}

FTwitchError UTwitchAuthComponent::GetLastError() const
{
    return m_LastError;
}

UWebBrowser* UTwitchAuthComponent::GetWebBrowser() const
{
    return m_WebBrowser;
}

#pragma endregion // Blueprint Interaction

#pragma endregion // Public

#pragma region Protected

void UTwitchAuthComponent::LogError(const FTwitchError& twitchError) const
{
    FString logMessage = twitchError.error + "(" + FString::FromInt(twitchError.status) + "): " + twitchError.message;
    UE_LOG(LogTemp, Error, TEXT("TwitchAuth: %s"), *logMessage);
}

#pragma endregion // Protected

#pragma region Private

#pragma  region Web Browser Widget

void UTwitchAuthComponent::HandleOnUrlChanged(const FText& InText)
{
    FString url = InText.ToString();
    if(url.Contains(UTwitchApi::ACCESS_TOKEN_URI_CONTAINS) == true)
    {
        OnAccessTokenPageLoaded.Broadcast();
        m_AccessToken = UTwitchApi::ExtractAccessToken(url);
        ExecuteGetUserRequest();
    }
    else
    {
        OnAuthenticationPageLoaded.Broadcast();
    }
}

#pragma endregion //Web Browser Widget

#pragma region Twitch HTTP API

void UTwitchAuthComponent::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    // When the response comes in we first check if it is valid.
    if(UTwitchApi::IsResponseValid(Response, bWasSuccessful) == true)
    {
        // Based on the last endpoint we send a request to we handle the response.
        switch(m_LastEndpoint)
        {
            case ETwitchEndpoint::User:
                m_LastEndpoint = ETwitchEndpoint::None;

                HandleGetUserResponse(Request, Response);
                break;

            case ETwitchEndpoint::Channels:
                m_LastEndpoint = ETwitchEndpoint::None;

                HandleGetChannelResponse(Request, Response);
                break;

            case ETwitchEndpoint::Subscriptions:
                m_LastEndpoint = ETwitchEndpoint::None;

                HandleCheckUserSubscriptionResponse(Request, Response);
                break;

            case ETwitchEndpoint::Following:
                m_LastEndpoint = ETwitchEndpoint::None;

                HandleCheckUserFollowingResponse(Request, Response);
                break;

            default:
                break;
        }
    }
    else
    {
        FString responseBody = Response->GetContentAsString();
        FJsonObjectConverter::JsonObjectStringToUStruct<FTwitchError>(responseBody, &m_LastError, 0, 0);

        switch(m_LastEndpoint)
        {
            case ETwitchEndpoint::Following:
                LogError(m_LastError);
                OnUserFollowingChannel.Broadcast(false, m_TwitchFollow);
                break;

            case ETwitchEndpoint::Subscriptions:
                LogError(m_LastError);
                OnUserSubscribedToChannel.Broadcast(false, m_TwitchSubscription);
                break;

            case ETwitchEndpoint::Channels:
                LogError(m_LastError);
                OnUserSubscribedToChannel.Broadcast(false, m_TwitchSubscription);
                break;

            case ETwitchEndpoint::User:
                LogError(m_LastError);
                OnUserAuthenticated.Broadcast(false);
                break;
        }
    }
}

#pragma endregion // HTTP API

#pragma region Twitch API Endpoints

void UTwitchAuthComponent::ExecuteGetUserRequest()
{
    // Create a request with the correct endpoint.
    TSharedRef<IHttpRequest> request = UTwitchApi::CreateHttpRequest(ClientId, m_AccessToken, UTwitchApi::USER_ENDPOINT, ETwitchHttpVerb::Get);
    request->OnProcessRequestComplete().BindUObject(this, &UTwitchAuthComponent::OnResponseReceived);

    // Set the last endpoint so that the response handler knows what to do.
    m_LastEndpoint = ETwitchEndpoint::User;

    // Send the request.
    request->ProcessRequest();
}

void UTwitchAuthComponent::HandleGetUserResponse(FHttpRequestPtr Request, FHttpResponsePtr Response)
{
    // Get the response body as a string.
    const FString responseBody = Response->GetContentAsString();

    // And convert the body to a struct.
    if(FJsonObjectConverter::JsonObjectStringToUStruct<FTwitchUser>(responseBody, &m_TwitchUser, 0, 0) == true)
    {
        OnUserAuthenticated.Broadcast(true);
    }
    else
    {
        OnUserAuthenticated.Broadcast(false);
    }
}

void UTwitchAuthComponent::ExecuteGetChannelRequest(const FString& ChannelName)
{
    const FString endpoint = UTwitchApi::CHANNEL_ENDPOINT + ChannelName;
    TSharedRef<IHttpRequest> request = UTwitchApi::CreateHttpRequest(ClientId, m_AccessToken, endpoint, ETwitchHttpVerb::Get);
    request->OnProcessRequestComplete().BindUObject(this, &UTwitchAuthComponent::OnResponseReceived);

    m_LastEndpoint = ETwitchEndpoint::Channels;

    request->ProcessRequest();
}

void UTwitchAuthComponent::HandleGetChannelResponse(FHttpRequestPtr Request, FHttpResponsePtr Response)
{
    // Get the response body as a string.
    FString responseBody = Response->GetContentAsString();
    responseBody = UTwitchApi::ExtractTwitchChannelUserFromResponseBody(responseBody);

    // And convert the body to a struct.
    if(FJsonObjectConverter::JsonObjectStringToUStruct<FTwitchChannelUser>(responseBody, &m_TwitchChannelUser, 0, 0) == true)
    {
        switch(m_TwitchRequest)
        {
            case ETwitchRequest::Subscription: ExecuteCheckUserSubscriptionRequest(m_TwitchUser, m_TwitchChannelUser);
                break;
            case ETwitchRequest::Following: ExecuteCheckUserFollowingRequest(m_TwitchUser, m_TwitchChannelUser);
                break;
        }

        m_TwitchRequest = ETwitchRequest::None;
    }
    else
    {
        FTwitchError twitchError;
        twitchError.error = "Not Found";
        twitchError.status = 404;
        twitchError.message = "Channel could not be found";

        m_LastError = twitchError;

        LogError(m_LastError);
        OnUserSubscribedToChannel.Broadcast(false, m_TwitchSubscription);
    }
}

void UTwitchAuthComponent::ExecuteCheckUserSubscriptionRequest(const FTwitchUser& TwitchUser, const FTwitchChannelUser& TwitchChannel)
{
    FString endpoint = UTwitchApi::SUBSCRIPTION_ENDPOINT;
    endpoint = endpoint.Replace(TEXT("$1"), *TwitchUser._id);
    endpoint = endpoint.Replace(TEXT("$2"), *TwitchChannel._id);

    TSharedRef<IHttpRequest> request = UTwitchApi::CreateHttpRequest(ClientId, m_AccessToken, endpoint, ETwitchHttpVerb::Get);
    request->OnProcessRequestComplete().BindUObject(this, &UTwitchAuthComponent::OnResponseReceived);

    m_LastEndpoint = ETwitchEndpoint::Subscriptions;

    request->ProcessRequest();
}

void UTwitchAuthComponent::HandleCheckUserSubscriptionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response)
{
    const FString responseBody = Response->GetContentAsString();

    // And convert the body to a struct.
    if(FJsonObjectConverter::JsonObjectStringToUStruct<FTwitchSubscription>(responseBody, &m_TwitchSubscription, 0, 0) == true)
    {
        OnUserSubscribedToChannel.Broadcast(true, m_TwitchSubscription);
    }
}

void UTwitchAuthComponent::ExecuteCheckUserFollowingRequest(const FTwitchUser& TwitchUser, const FTwitchChannelUser& TwitchChannel)
{
    FString endpoint = UTwitchApi::FOLLOWING_ENDPOINT;
    endpoint = endpoint.Replace(TEXT("$1"), *TwitchUser._id);
    endpoint = endpoint.Replace(TEXT("$2"), *TwitchChannel._id);

    TSharedRef<IHttpRequest> request = UTwitchApi::CreateHttpRequest(ClientId, m_AccessToken, endpoint, ETwitchHttpVerb::Get);
    request->OnProcessRequestComplete().BindUObject(this, &UTwitchAuthComponent::OnResponseReceived);

    m_LastEndpoint = ETwitchEndpoint::Following;

    request->ProcessRequest();
}

void UTwitchAuthComponent::HandleCheckUserFollowingResponse(FHttpRequestPtr Request, FHttpResponsePtr Response)
{
    const FString responseBody = Response->GetContentAsString();

    // And convert the body to a struct.
    if(FJsonObjectConverter::JsonObjectStringToUStruct<FTwitchFollow>(responseBody, &m_TwitchFollow, 0, 0) == true)
    {
        OnUserFollowingChannel.Broadcast(true, m_TwitchFollow);
    }
}

#pragma endregion // Twitch API Endpoints

#pragma endregion // Private