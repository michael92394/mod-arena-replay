CREATE TABLE IF NOT EXISTS `character_arena_replays` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `arenaTypeId` INT NULL DEFAULT NULL,
  `typeId` INT NULL DEFAULT NULL,
  `contentSize` INT NULL DEFAULT NULL,
  `contents` LONGTEXT NULL,
  `mapId` INT NULL DEFAULT NULL,
  `winnerTeamName` VARCHAR(255) NULL DEFAULT NULL,
  `winnerTeamRating` INT NULL DEFAULT NULL,
  `winnerTeamMMR` INT NULL DEFAULT NULL,
  `loserTeamName` VARCHAR(255) NULL DEFAULT NULL,
  `loserTeamRating` INT NULL DEFAULT NULL,
  `loserTeamMMR` INT NULL DEFAULT NULL,
  `winnerPlayerGuids` VARCHAR(255) NULL DEFAULT NULL,
  `loserPlayerGuids` VARCHAR(255) NULL DEFAULT NULL,
  `winnerActorTrack` LONGTEXT NULL,
  `loserActorTrack` LONGTEXT NULL,
  `timesWatched` INT NOT NULL DEFAULT 0,
  `timestamp` TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE = InnoDB CHARACTER SET = utf8mb4 COLLATE = utf8mb4_unicode_ci ROW_FORMAT = DYNAMIC;

ALTER TABLE `character_arena_replays`
  ADD INDEX `idx_arena_type_timestamp` (`arenaTypeId`, `timestamp`),
  ADD INDEX `idx_times_watched_rating` (`timesWatched`, `winnerTeamRating`),
  ADD INDEX `idx_timestamp` (`timestamp`);

-- Existing installs:
-- ALTER TABLE `character_arena_replays` ADD COLUMN `winnerActorTrack` LONGTEXT NULL AFTER `loserPlayerGuids`;
-- ALTER TABLE `character_arena_replays` ADD COLUMN `loserActorTrack` LONGTEXT NULL AFTER `winnerActorTrack`;
