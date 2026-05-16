-- RTG Arena Replay 5.5.24 - packet-quality replay gate metadata.
-- Existing rows default to legacy_unqualified and hidden from packet-quality browser filters.

SET @arenaReplayDb := DATABASE();

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'replayQuality'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `replayQuality` VARCHAR(32) NOT NULL DEFAULT ''legacy_unqualified'' AFTER `actorAppearanceSnapshots`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'qualityReason'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `qualityReason` VARCHAR(255) NOT NULL DEFAULT ''legacy_or_missing_metadata'' AFTER `replayQuality`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'packetQualified'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `packetQualified` TINYINT(1) NOT NULL DEFAULT 0 AFTER `qualityReason`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'visibleInBrowser'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `visibleInBrowser` TINYINT(1) NOT NULL DEFAULT 0 AFTER `packetQualified`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'team0PacketCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `team0PacketCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `visibleInBrowser`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'team1PacketCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `team1PacketCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `team0PacketCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'neutralPacketCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `neutralPacketCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `team1PacketCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'skippedPacketCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `skippedPacketCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `neutralPacketCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'team0RealPlayerCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `team0RealPlayerCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `skippedPacketCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'team1RealPlayerCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `team1RealPlayerCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `team0RealPlayerCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'winnerPacketCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `winnerPacketCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `team1RealPlayerCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'loserPacketCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `loserPacketCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `winnerPacketCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'winnerRealPlayerCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `winnerRealPlayerCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `loserPacketCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND COLUMN_NAME = 'loserRealPlayerCount'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `loserRealPlayerCount` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `winnerRealPlayerCount`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.STATISTICS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replays' AND INDEX_NAME = 'idx_replay_quality_visible'),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD INDEX `idx_replay_quality_visible` (`packetQualified`, `visibleInBrowser`, `replayQuality`)'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
