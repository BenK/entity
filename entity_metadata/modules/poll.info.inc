<?php
// $Id$

/**
 * @file
 * Provides info about poll nodes.
 */

/**
 * Implements hook_entity_property_info_alter() on top of poll module.
 * @see entity_metadata_entity_property_info_alter()
 */
function entity_metadata_poll_entity_property_info_alter(&$info) {
  $properties = &$info['node']['bundles']['poll']['properties'];

  $properties['poll-votes'] = array(
    'label' => t("Poll votes"),
    'description' => t("The number of votes that have been cast on a poll node."),
    'type' => 'integer',
    'getter callback' => 'entity_metadata_poll_node_get_properties',
  );
  $properties['poll-winner'] = array(
    'label' => t("Poll winner"),
    'description' => t("The winning poll answer."),
    'getter callback' => 'entity_metadata_poll_node_get_properties',
    'sanitize' => 'filter_xss',
  );
  $properties['poll-winner-votes'] = array(
    'label' => t("Poll winner votes"),
    'description' => t("The number of votes received by the winning poll answer."),
    'type' => 'integer',
    'getter callback' => 'entity_metadata_poll_node_get_properties',
  );
  $properties['poll-winner-percent'] = array(
    'label' => t("Poll winner percent"),
    'description' => t("The percentage of votes received by the winning poll answer."),
    'getter callback' => 'entity_metadata_poll_node_get_properties',
    'type' => 'decimal',
  );
  $properties['poll-duration'] = array(
    'label' => t("Poll duration"),
    'description' => t("The length of time the poll node is set to run."),
    'getter callback' => 'entity_metadata_poll_node_get_properties',
    'type' => 'duration',
  );
}
