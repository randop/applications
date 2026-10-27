import { z } from 'zod';

export const AnimeImageSchema = z.object({
  id: z.string().min(1).max(80),
  type: z.string().min(1).max(80),
  url: z.string().url(),
  alt: z.string().max(500).default(''),
  localPath: z.string().optional(),
});

export const AnimeMetadataSchema = z.object({
  title: z.string().trim().min(1).max(300),
  originalTitle: z.string().trim().max(300).default(''),
  alternativeTitle: z.string().trim().max(500).default(''),
  releaseDate: z.string().regex(/^\d{4}(-\d{2}(-\d{2})?)?$/).optional(),
  genres: z.array(z.string().trim().min(1).max(80)).max(30).default([]),
  images: z.array(AnimeImageSchema.omit({ localPath: true })).max(30).default([]),
  synopsis: z.string().trim().max(12000).default(''),
});

export const CreateAnimeFromUrlSchema = z.object({
  url: z.string().url().max(2048),
});

export const AnimeRecordSchema = AnimeMetadataSchema.extend({
  id: z.string().uuid(),
  sourceUrl: z.string().url(),
  images: z.array(AnimeImageSchema).max(30),
  createdAt: z.string().datetime(),
  updatedAt: z.string().datetime(),
});

export type AnimeMetadata = z.infer<typeof AnimeMetadataSchema>;
export type AnimeImage = z.infer<typeof AnimeImageSchema>;
export type AnimeRecord = z.infer<typeof AnimeRecordSchema>;
export type CreateAnimeFromUrl = z.infer<typeof CreateAnimeFromUrlSchema>;
