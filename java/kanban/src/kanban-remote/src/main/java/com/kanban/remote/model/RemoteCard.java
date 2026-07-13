package com.kanban.remote.model;

import org.springframework.data.annotation.Id;
import org.springframework.data.annotation.Version;
import org.springframework.data.mongodb.core.mapping.Document;

import java.time.Instant;

/**
 * Remote ciphertext-only document. The remote API never holds encKey or
 * encSalt. title/desc are always encrypted blobs from the perspective of
 * this service; it has no capability to decrypt them.
 */
@Document(collection = "remote_cards")
public class RemoteCard {

    @Id
    private String id;
    private String cardId;
    private String boardId;

    private String titleCipher;
    private String titleIv;
    private String descCipher;
    private String descIv;

    private String status;
    private int position;

    @Version
    private Long version;

    private Instant createdAt = Instant.now();
    private Instant updatedAt = Instant.now();

    public String getId() { return id; }
    public void setId(String id) { this.id = id; }
    public String getCardId() { return cardId; }
    public void setCardId(String cardId) { this.cardId = cardId; }
    public String getBoardId() { return boardId; }
    public void setBoardId(String boardId) { this.boardId = boardId; }
    public String getTitleCipher() { return titleCipher; }
    public void setTitleCipher(String titleCipher) { this.titleCipher = titleCipher; }
    public String getTitleIv() { return titleIv; }
    public void setTitleIv(String titleIv) { this.titleIv = titleIv; }
    public String getDescCipher() { return descCipher; }
    public void setDescCipher(String descCipher) { this.descCipher = descCipher; }
    public String getDescIv() { return descIv; }
    public void setDescIv(String descIv) { this.descIv = descIv; }
    public String getStatus() { return status; }
    public void setStatus(String status) { this.status = status; }
    public int getPosition() { return position; }
    public void setPosition(int position) { this.position = position; }
    public Long getVersion() { return version; }
    public void setVersion(Long version) { this.version = version; }
    public Instant getCreatedAt() { return createdAt; }
    public void setCreatedAt(Instant createdAt) { this.createdAt = createdAt; }
    public Instant getUpdatedAt() { return updatedAt; }
    public void setUpdatedAt(Instant updatedAt) { this.updatedAt = updatedAt; }
}
