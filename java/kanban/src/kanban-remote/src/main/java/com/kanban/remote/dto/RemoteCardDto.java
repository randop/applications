package com.kanban.remote.dto;

import java.time.Instant;

public record RemoteCardDto(
        String id,
        String cardId,
        String boardId,
        String titleCipher,
        String titleIv,
        String descCipher,
        String descIv,
        String status,
        int position,
        Instant createdAt,
        Instant updatedAt
) {}
