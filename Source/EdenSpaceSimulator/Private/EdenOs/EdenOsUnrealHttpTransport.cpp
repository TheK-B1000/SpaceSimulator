// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsUnrealHttpTransport.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

bool FEdenOsUnrealHttpTransport::SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Request.Url);
	HttpRequest->SetVerb(Request.Verb);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (!Request.AuthorizationBearerJwt.IsEmpty())
	{
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Request.AuthorizationBearerJwt));
	}
	HttpRequest->SetContentAsString(Request.BodyJson);
	HttpRequest->SetTimeout(Request.TimeoutSeconds);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[Completion = MoveTemp(Completion)](
			FHttpRequestPtr CompletedRequest,
			FHttpResponsePtr Response,
			bool bConnectedSuccessfully) mutable
		{
			(void)CompletedRequest;

			if (!bConnectedSuccessfully || !Response.IsValid())
			{
				Completion.ExecuteIfBound(FEdenOsHttpResult::Failed(0, TEXT("Network request failed.")));
				return;
			}

			const int32 ResponseCode = Response->GetResponseCode();
			const FString ResponseBody = Response->GetContentAsString();
			if (FEdenOsTransportModel::IsSuccessfulHttpStatus(ResponseCode))
			{
				Completion.ExecuteIfBound(FEdenOsHttpResult::Succeeded(ResponseCode, ResponseBody));
				return;
			}

			Completion.ExecuteIfBound(
				FEdenOsHttpResult::Failed(
					ResponseCode,
					FString::Printf(TEXT("HTTP status %d"), ResponseCode),
					ResponseBody));
		});

	if (!HttpRequest->ProcessRequest())
	{
		return false;
	}

	return true;
}
