package com.kanban.local.dto;

import java.time.Instant;

public record RemoteCardPayload(
        String cardId,
        String boardId,
        String titleCipher,
        String titleIv,
        String descCipher,
        String descIv,
        String status,
        int position,
        Instant updatedAt
) {}
