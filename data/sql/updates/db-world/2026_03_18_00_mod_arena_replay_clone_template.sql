-- Arena Replay clone-scene world objects.

DELETE FROM `creature_template` WHERE `entry` = 98501;
INSERT INTO `creature_template` (`entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`, `KillCredit1`, `KillCredit2`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `speed_swim`, `speed_flight`, `detection_range`, `scale`, `rank`, `dmgschool`, `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`, `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`, `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`, `ScriptName`, `VerifiedBuild`) VALUES
(98501, 0, 0, 0, 0, 0, 'Replay Actor Clone', '', '', 0, 80, 80, 0, 35, 0, 1, 1.14286, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 2, '', 0);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 98501);
INSERT INTO `creature_template_model`(`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`) values
(98501,0,27800,1,1,0);
