package com.kanban.local.model;

import org.springframework.data.annotation.Id;
import org.springframework.data.annotation.Version;
import org.springframework.data.mongodb.core.index.Indexed;
import org.springframework.data.mongodb.core.mapping.Document;

import java.time.Instant;

@Document(collection = "cards")
public class Card {

    @Id
    private String id;

    @Indexed
    private String boardId;

    private String status = "TODO";
    private int position;

    // Ciphertext only - plaintext title/description are never persisted.
    private String titleCipher;
    private String titleIv;
    private String descCipher;
    private String descIv;

    // Per-document secret material. NEVER sent to the remote API.
    private String encKey;
    private String encSalt;

    private String remoteId;
    private SyncStatus syncStatus = SyncStatus.PENDING;

    @Version
    private Long version;

    private Instant createdAt = Instant.now();
    private Instant updatedAt = Instant.now();

    public String getId() { return id; }
    public void setId(String id) { this.id = id; }
    public String getBoardId() { return boardId; }
    public void setBoardId(String boardId) { this.boardId = boardId; }
    public String getStatus() { return status; }
    public void setStatus(String status) { this.status = status; }
    public int getPosition() { return position; }
    public void setPosition(int position) { this.position = position; }
    public String getTitleCipher() { return titleCipher; }
    public void setTitleCipher(String titleCipher) { this.titleCipher = titleCipher; }
    public String getTitleIv() { return titleIv; }
    public void setTitleIv(String titleIv) { this.titleIv = titleIv; }
    public String getDescCipher() { return descCipher; }
    public void setDescCipher(String descCipher) { this.descCipher = descCipher; }
    public String getDescIv() { return descIv; }
    public void setDescIv(String descIv) { this.descIv = descIv; }
    public String getEncKey() { return encKey; }
    public void setEncKey(String encKey) { this.encKey = encKey; }
    public String getEncSalt() { return encSalt; }
    public void setEncSalt(String encSalt) { this.encSalt = encSalt; }
    public String getRemoteId() { return remoteId; }
    public void setRemoteId(String remoteId) { this.remoteId = remoteId; }
    public SyncStatus getSyncStatus() { return syncStatus; }
    public void setSyncStatus(SyncStatus syncStatus) { this.syncStatus = syncStatus; }
    public Long getVersion() { return version; }
    public void setVersion(Long version) { this.version = version; }
    public Instant getCreatedAt() { return createdAt; }
    public void setCreatedAt(Instant createdAt) { this.createdAt = createdAt; }
    public Instant getUpdatedAt() { return updatedAt; }
    public void setUpdatedAt(Instant updatedAt) { this.updatedAt = updatedAt; }
}
