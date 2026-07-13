package com.kanban.local.dto;

import jakarta.validation.constraints.NotBlank;

public record CardRequest(
        @NotBlank String boardId,
        @NotBlank String title,
        String description,
        String status,
        int position
) {}
