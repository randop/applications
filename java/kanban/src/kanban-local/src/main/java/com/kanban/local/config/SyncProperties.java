package com.kanban.local.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "sync")
public class SyncProperties {

    private String remoteBaseUrl = "http://localhost:8081";
    private long fixedDelayMs = 30000;
    private boolean enabled = true;

    public String getRemoteBaseUrl() { return remoteBaseUrl; }
    public void setRemoteBaseUrl(String remoteBaseUrl) { this.remoteBaseUrl = remoteBaseUrl; }
    public long getFixedDelayMs() { return fixedDelayMs; }
    public void setFixedDelayMs(long fixedDelayMs) { this.fixedDelayMs = fixedDelayMs; }
    public boolean isEnabled() { return enabled; }
    public void setEnabled(boolean enabled) { this.enabled = enabled; }
}
