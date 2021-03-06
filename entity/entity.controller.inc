<?php
// $Id$

/**
 * @file
 * Provides a controller building upon the core controller but providing more
 * features like full CRUD functionality.
 */

/**
 * Interface for EntityControllers compatible with the entity API.
 */
interface EntityAPIControllerInterface extends DrupalEntityControllerInterface {

  /**
   * Delete permanently saved entities.
   *
   * @param $ids
   *   An array of entity IDs.
   * @return
   *   FALSE, if deleting failed. Else NULL.
   */
  public function delete($ids);

  /**
   * Invokes a hook on behalf the entity. For hooks that have a respective
   * field API attacher like insert/update/.. the attacher is called too.
   */
  public function invoke($hook, $entity);

  /**
   * Permanently saves the given entity.
   *
   * @return
   *   Failure to write a record will return FALSE. Otherwise SAVED_NEW or
   *   SAVED_UPDATED is returned depending on the operation performed.
   */
  public function save($entity);

  /**
   * Create a new entity.
   *
   * @param $values
   *   An array of values to set, keyed by property name.
   * @return
   *   A new instance of the entity type.
   */
  public function create($values = array());


  /**
   * Exports an entity.
   *
   * @return
   *   The exported entity as code.
   */
  public function export($entity);
}

/**
 * A controller implementing EntityAPIControllerInterface and being able to
 * deal with exportable entities.
 */
class EntityAPIController extends DrupalDefaultEntityController implements EntityAPIControllerInterface {

  protected $defaultEntities, $defaultsFiltered = FALSE;
  protected $nameKey, $statusKey, $bundleKey;

  /**
   * Overridden.
   * @see DrupalDefaultEntityController#__construct()
   *
   * Allows specifying a name key serving as uniform identifier for this entity
   * type while still internally we are using numeric identifieres.
   */
  public function __construct($entityType) {
    parent::__construct($entityType);
    // Use the name key as primary identifier.
    $this->nameKey = isset($this->entityInfo['entity keys']['name']) ? $this->entityInfo['entity keys']['name'] : $this->idKey;
    if (isset($this->entityInfo['exportable'])) {
      $this->statusKey = isset($this->entityInfo['export']['status key']) ? $this->entityInfo['export']['status key'] : 'status';
    }
    // If this is the bundle of another entity, set the bundle key.
    if (isset($this->entityInfo['bundle of'])) {
      $info = entity_get_info($this->entityInfo['bundle of']);
      $this->bundleKey = $info['bundle keys']['bundle'];
    }
  }

  /**
   * Builds and executes the query for loading.
   *
   * @return The results in a Traversable object.
   */
  public function query($ids, $conditions, $revision_id = FALSE) {
    // Build the query.
    $query = $this->buildQuery($ids, $conditions, $revision_id);
    $result = $query->execute();
    if (!empty($this->entityInfo['entity class'])) {
      $result->setFetchMode(PDO::FETCH_CLASS, $this->entityInfo['entity class'], array(array(), $this->entityType));
    }
    return $result;
  }

  protected function buildQuery($ids, $conditions = array(), $revision_id = FALSE) {
    // Add the id condition ourself, as we might have a separate name key.
    $query = parent::buildQuery(array(), $conditions, $revision_id);
    if ($ids) {
      $query->condition("base.{$this->nameKey}", $ids, 'IN');
    }
    return $query;
  }

  /**
   * Overridden.
   * @see DrupalDefaultEntityController#load($ids, $conditions)
   *
   * In contrast to the parent implementation we factor out query execution, so
   * fetching can be further customized easily. Also we add any in code defined
   * objects.
   */
  public function load($ids = array(), $conditions = array()) {
    $entities = array();

    // Revisions are not statically cached, and require a different query to
    // other conditions, so separate the revision id into its own variable.
    if ($this->revisionKey && isset($conditions[$this->revisionKey])) {
      $revision_id = $conditions[$this->revisionKey];
      unset($conditions[$this->revisionKey]);
    }
    else {
      $revision_id = FALSE;
    }

    // Create a new variable which is either a prepared version of the $ids
    // array for later comparison with the entity cache, or FALSE if no $ids
    // were passed. The $ids array is reduced as items are loaded from cache,
    // and we need to know if it's empty for this reason to avoid querying the
    // database when all requested entities are loaded from cache.
    $passed_ids = !empty($ids) ? array_flip($ids) : FALSE;
    // Try to load entities from the static cache, if the entity type supports
    // static caching.
    if ($this->cache) {
      $entities = $this->cacheGet($ids, $conditions);
      // If any entities were loaded, remove them from the ids still to load.
      if ($passed_ids) {
        $ids = array_keys(array_diff_key($passed_ids, $entities));
      }
    }
    if (!empty($this->entityInfo['exportable']) && ($ids !== array() || !$passed_ids)) {
      // Add default entities defined in code.
      $entities += $this->getDefaults($ids, $conditions);
    }

    // Load any remaining entities from the database. This is the case if $ids
    // is set to FALSE (so we load all entities), if there are any ids left to
    // load, if loading a revision, or if $conditions was passed without $ids.
    if ($ids === FALSE || $ids || $revision_id || ($conditions && !$passed_ids)) {
      $schema = drupal_get_schema($this->entityInfo['base table']);
      $queried_entities = array();
      foreach ($this->query($ids, $conditions, $revision_id) as $record) {
        // Care for serialized columns.
        foreach ($schema['fields'] as $field => $info) {
          if (!empty($info['serialize']) && isset($record->$field)) {
            $record->$field = unserialize($record->$field);
            // Support automatic merging of 'data' fields into the entity.
            if (!empty($info['merge']) && is_array($record->$field)) {
              foreach ($record->$field as $key => $value) {
                $record->$key = $value;
              }
              unset($record->$field);
            }
          }
        }
        if (isset($this->statusKey)) {
          // Care for setting the status key properly.
          $record->{$this->statusKey} |= ENTITY_IN_DB;
          $id = $record->{$this->nameKey};
          if (isset($this->defaultEntities[$id])) {
            $record->{$this->statusKey} |= ENTITY_IN_CODE;
            unset($entities[$id]);
          }
        }
        $queried_entities[$record->{$this->nameKey}] = $record;
      }
    }

    // Pass all entities loaded from the database through $this->attachLoad(),
    // which attaches fields (if supported by the entity type) and calls the
    // entity type specific load callback, for example hook_node_load().
    if (!empty($queried_entities)) {
      $this->attachLoad($queried_entities, $revision_id);
      $entities += $queried_entities;
    }

    if ($this->cache) {
      // Add entities to the cache if we are not loading a revision.
      if (!empty($queried_entities) && !$revision_id) {
        $this->cacheSet($queried_entities);
      }
    }
    // Ensure that the returned array is ordered the same as the original
    // $ids array if this was passed in and remove any invalid ids.
    if ($passed_ids) {
      // Remove any invalid ids from the array.
      $passed_ids = array_intersect_key($passed_ids, $entities);
      foreach ($entities as $entity) {
        $passed_ids[$entity->{$this->nameKey}] = $entity;
      }
      $entities = $passed_ids;
    }
    return $entities;
  }

  /**
   * For exportables call the hook to get all default entities.
   */
  protected function getDefaults($ids, $conditions = array()) {
    if (!isset($this->defaultEntities)) {
      $this->defaultEntities = array();
      if (!empty($this->entityInfo['exportable'])) {
        $this->entityInfo += array('export' => array());
        $this->entityInfo['export'] += array('default hook' => 'default_' . $this->entityType);
        if ($hook = $this->entityInfo['export']['default hook']) {
          $this->defaultEntities = module_invoke_all($hook);
          drupal_alter($hook, $this->defaultEntities);

          foreach ($this->defaultEntities as $id => $entity) {
            $entity->{$this->statusKey} |= ENTITY_IN_CODE;
            $entity->{$this->nameKey} = $id;
          }
        }
      }
    }
    $entities = $ids ? array_intersect_key($this->defaultEntities, array_flip($ids)) : $this->defaultEntities;
    if (!$this->defaultsFiltered && $conditions && $entities) {
      // Remove overridden entities. This is not necessary for pure id based
      // lookups though, as in this case overridden entities would get
      // incorporated later anyway.
      foreach ($this->query(array_keys($this->defaultEntities), array()) as $record) {
        $this->defaultEntities[$record->{$this->nameKey}] = FALSE;
        unset($entities[$record->{$this->nameKey}]);
      }
      $this->cacheSet(array_filter($this->defaultEntities));
      $this->defaultsFiltered = TRUE;
    }
    return $this->applyConditions($entities, $conditions);
  }

  protected function applyConditions($entities, $conditions = array()) {
    if ($conditions) {
      foreach ($entities as $key => $entity) {
        $entity_values = (array) $entity;
        if (array_diff_assoc($conditions, $entity_values)) {
          unset($entities[$key]);
        }
      }
    }
    return $entities;
  }

  /**
   * Overridden.
   * @see includes/DrupalDefaultEntityController#cacheGet($ids, $conditions)
   *
   * If there is nameKey given, we index our entities by this key. This
   * overrides cacheGet() to respect that when applying $conditions.
   */
  protected function cacheGet($ids, $conditions = array()) {
    return $this->applyConditions(parent::cacheGet($ids), $conditions);
  }

  public function resetCache() {
    $this->entityCache = array();
    unset($this->defaultEntities);
    $this->defaultsFiltered = FALSE;
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function invoke($hook, $entity) {
    if (!empty($this->entityInfo['fieldable']) && function_exists($function = 'field_attach_' . $hook)) {
      $function($this->entityType, $entity);
    }
    if (isset($this->entityInfo['bundle of']) && $type = $this->entityInfo['bundle of']) {
      // Call field API bundle attachers for the entity we are a bundle of.
      if ($hook == 'insert') {
        field_attach_create_bundle($type, $entity->{$this->bundleKey});
      }
      elseif ($hook == 'delete') {
        field_attach_delete_bundle($type, $entity->{$this->bundleKey});
      }
      elseif ($hook == 'update' && $id = $entity->{$this->idKey}) {
        $entities = entity_load($this->entityType, array($id));
        if ($entities[$id]->{$this->bundleKey} != $entity->{$this->bundleKey}) {
          field_attach_rename_bundle($type, $entities[$id]->{$this->bundleKey}, $entity->{$this->bundleKey});
        }
      }
    }
    module_invoke_all($this->entityType . '_' . $hook, $entity);
    if ($hook == 'insert' || $hook == 'update') {
      module_invoke_all('entity_' . $hook, $entity, $this->entityType);
    }
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function delete($ids) {
    $entities = $this->load($ids);

    try {
      db_delete($this->entityInfo['base table'])
        ->condition($this->nameKey, $ids, 'IN')
        ->execute();

      foreach ($entities as $entity) {
        $this->invoke('delete', $entity);
      }
      // Ignore slave server temporarily.
      db_ignore_slave();
    }
    catch (Exception $e) {
      $transaction->rollback($this->entityType, $e->getMessage(), array(), WATCHDOG_ERROR);
      return FALSE;
    }
    $this->resetCache();
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function save($entity) {
    $transaction = db_transaction();

    try {
      $this->invoke('presave', $entity);

      if (!empty($entity->{$this->idKey}) && empty($entity->is_new)) {
        $return = drupal_write_record($this->entityInfo['base table'], $entity, $this->idKey);
        $this->invoke('update', $entity);
      }
      else {
        $return = drupal_write_record($this->entityInfo['base table'], $entity);
        $this->invoke('insert', $entity);
      }
      // Ignore slave server temporarily.
      db_ignore_slave();

      return $return;
    }
    catch (Exception $e) {
      $transaction->rollback($this->entityType, $e->getMessage(), array(), WATCHDOG_ERROR);
      return FALSE;
    }
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function create($values = array()) {
    if (isset($this->entityInfo['entity class']) && $class = $this->entityInfo['entity class']) {
      // Add is_new property if it is not set.
      $values += array('is_new' => TRUE);
      return new $class($values, $this->entityType);
    }
    return (object) $values;
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function export($entity) {
    $vars = get_object_vars($entity);
    unset($vars[$this->statusKey]);
    return "entity_create('$this->entityType', " . entity_var_export($vars) . ");";
  }
}
