#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer server;
extern bool motorsRunning;

void handleRoot();
void handleStart();
void handleStop();
void handleHome();
void handleStatus();
void handleNotFound();

void setupWebServer();