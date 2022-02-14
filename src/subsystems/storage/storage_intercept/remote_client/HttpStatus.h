//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
enum class HttpStatus
{
    /* Successful status codes */
    Ok                              = 200,
    Created                         = 201,
    Accepted                        = 202,
    NonAuthoritativeInformation     = 203,
    NoContent                       = 204,
    ResetContent                    = 205,
    PartialContent                  = 206,

    /* Redirection status codes */
    MultipleChoice                  = 300,
    MovedPermanently                = 301,
    Found                           = 302,
    SeeOther                        = 303,
    NotModified                     = 304,
    UseProxy                        = 305,
    Unused                          = 306,
    TemporaryRedirect               = 307,
    PermanentRedirect               = 308,

    /* Client error codes */
    BadRequest                      = 400,
    Unauthorized                    = 401,
    PaymentRequired                 = 402,
    Forbidden                       = 403,
    NotFound                        = 404,
    MethodNotAllowed                = 405,
    NotAcceptable                   = 406,
    ProxyAuthenticationRequired     = 407,
    RequestTimeout                  = 408,
    Conflict                        = 409,
    Gone                            = 410,
    LengthRequired                  = 411,
    PreConditionFailed              = 412,
    PayloadTooLarge                 = 413,
    UriTooLong                      = 414,
    UnsupportedMediaType            = 415,
    RangeNotSatisfiable             = 416,
    ExpectationFailed               = 417,
    ImATeapot                       = 418,
    MisdirectedRequest              = 421,
    TooEarly                        = 425,
    UpgradeRequired                 = 426,
    PreConditionRequired            = 428,
    TooManyRequests                 = 429,
    RequestHeaderFieldsTooLarge     = 431,
    UnavailableForLegalReasons      = 451,

    /* Server error codes */
    InternalServerError             = 500,
    NotImplemented                  = 501,
    BadGateway                      = 502,
    ServiceUnavailable              = 503,
    GatewayTimeout                  = 504,
    HttpVersionNotSupported         = 505,
    VariantAlsoNegotiates           = 506,
    NotExtended                     = 510,
    NetworkAuthenticationRequired   = 511
};
//---------------------------------------------------------------------------------------------------------------------