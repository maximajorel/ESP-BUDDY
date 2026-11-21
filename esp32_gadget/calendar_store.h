// The calendar's credentials, in their own NVS namespace.
//
// The settings blob is versioned by size, so adding fields there would invalidate
// every stored WiFi password on upgrade; the same reason Spotify and the rotation
// keys live apart. What is stored: the OAuth client pair, the long-lived refresh
// token, the selected calendar and two owner preferences. The access token stays
// in RAM, since it expires every hour and a flash write per hour buys nothing.
#pragma once

#include <Arduino.h>

#include "calendar_model.h"

#define CALENDAR_CLIENT_ID_MAX 120
#define CALENDAR_CLIENT_SECRET_MAX 120
#define CALENDAR_TOKEN_MAX 511

// The panel's fallback reminder for events whose calendar declares no default.
#define CALENDAR_FALLBACK_LEAD_DEFAULT 10
#define CALENDAR_FALLBACK_LEAD_MIN 1
#define CALENDAR_FALLBACK_LEAD_MAX 720

#define CALENDAR_GRACE_DEFAULT 5
#define CALENDAR_GRACE_MIN 0
#define CALENDAR_GRACE_MAX 120

void calendarStoreBegin();

const char *calendarStoreClientId();
const char *calendarStoreClientSecret();
const char *calendarStoreRefreshToken();
const char *calendarStoreSelectedId();
const char *calendarStoreSelectedName();

// Shape checks only: a client id must be a printable token (Google's end with
// `.apps.googleusercontent.com`, but that suffix is not enforced in case the
// console changes it). An invalid value is rejected instead of stored, so a typo
// cannot turn into a certificate-looking failure later.
bool calendarStoreSetClientId(const char *clientId);
bool calendarStoreSetClientSecret(const char *clientSecret);
bool calendarStoreSetRefreshToken(const char *token);
// Stores the chosen calendar and the name shown in the panel. An empty id is
// rejected: "no calendar" is represented by absence, never by an empty key.
bool calendarStoreSetSelected(const char *id, const char *name);

int calendarStoreFallbackLeadMin();
bool calendarStoreSetFallbackLeadMin(int minutes);
int calendarStoreGraceMin();
bool calendarStoreSetGraceMin(int minutes);

// True when the three credentials that make a request possible are stored.
bool calendarStoreConfigured();
// True when Google already granted the permission.
bool calendarStoreAuthorized();

// Drops the refresh token but keeps the client pair and the selection: this is
// what a revoked or expired permission needs.
void calendarStoreForgetToken();
// Drops everything the panel can edit.
void calendarStoreForgetAll();
