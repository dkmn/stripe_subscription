<?php
/**
 * @file
 * Callbacks for the stripe_subscription module.
 */

/**
 * Subscription form.
 */
function stripe_subscription_subscription_form($form, &$form_state, $uid = NULL) {
  stripe_initialize();

  $plan_list = _stripe_subscription_get_plans();
  if (count($plan_list) == 0) {
    return $form;
  }

  if (!isset($uid)) {
    global $user;
    $uid = $user->uid;
  }
  $account = user_load($uid);

  $stripe_form = stripe_form_default_items();
  $form = array_merge($form, $stripe_form);

  // Test to see if there is a customer_id for this user already
  $livemode = stripe_subscription_live() ? 1 : 0;
  $customer = NULL;
  $subscription = NULL;
  $customer_id = db_select('stripe_customers', 'sc')
    ->fields('sc', array('customer_id'))
    ->condition('sc.uid', $account->uid)
    ->condition('sc.livemode', $livemode)
    ->execute()->fetchField();

  // If the customer ID exists, request the customer
  // object to modify the subscription form.
  if (!empty($customer_id)) {
    try {
      $customer = Stripe_Customer::retrieve($customer_id);
      if (!empty($customer->subscription)) {
        $subscription = $customer->subscription;
      }
    }
    catch(Exception $e) {
      //
    }
  }

  $form['customer_id'] = array(
    '#type' => 'hidden',
    '#value' => $customer_id
  );

  $form['uid'] = array(
    '#type' => 'hidden',
    '#value' => $account->uid
  );

  $form['subscription'] = array(
    '#type' => 'fieldset',
    '#title' => 'Subscription Info'
  );

  $new_subscription = TRUE;

  if (is_object($subscription)) {
    $new_subscription = FALSE;

    // Build subscription status table.
    $header = array('Status', 'Current Plan', 'Next Invoice', '');
    $status = isset($subscription->status) ? ucfirst($subscription->status) : t('Inactive');
    $current = isset($subscription->plan) ? stripe_subscription_plan_name($subscription->plan) : t('None');
    $next = format_date($subscription->current_period_end+1, 'custom', 'm/d/Y');
    $ops = l('Cancel', current_path() . '/cancel');

    $rows = array(array($status, $current, $next, $ops));

    $form['subscription']['status'] = array(
      '#markup' => theme('table', array('header' => $header, 'rows' => $rows))
    );

    if (isset($subscription->plan)) {
      $form['previous_plan_name'] = array(
        '#type' => 'hidden',
        '#value' => $subscription->plan->name
      );
    }
  }
  else {
    if (isset($form['#form_id']) && $form['#form_id'] != 'user_register_form') {
      $form['subscription']['status'] = array(
        '#markup' => '<p>' . t('Currently not subscribed') . '</p>'
      );
    }
  }

  $form['new_subscription'] = array(
    '#type' => 'hidden',
    '#value' => $new_subscription
  );

  $action =  is_object($subscription) && isset($subscription->status) ? 'Update Your' : 'Select a';
  $plan_options = array('_none' => t('--'));
  if (isset($form['#form_id']) && $form['#form_id'] == 'user_register_form' && variable_get('stripe_subscription_reg_req', 0) == 1) {
    unset($plan_options['_none']);
  }
  $current_plan = is_object($subscription) && isset($subscription->plan) ? $subscription->plan->id : NULL;

  $available_plan_count = 0;
  foreach ($plan_list as $plan) {
    if ($plan->id == $current_plan) {
      continue;
    }
    $available_plan_count++;
    $plan_options[$plan->id] = stripe_subscription_plan_name($plan);
  }

  // Only show the select if there's another plan available to change to.
  $plan_keys = array_keys($plan_options);
  if ($available_plan_count > 2) {
    $form['subscription']['plan_id'] = array(
      '#type' => 'select',
      '#title' => t('%action Subscription Plan', array('%action' => $action)),
      '#options' => $plan_options,
      '#default_value' => !isset($current_plan) ? $plan_keys[1] : NULL
    );
  }
  elseif ($available_plan_count == 1) {
    if (isset($form['#form_id']) && $form['#form_id'] == 'user_register_form') {
      $form['subscription']['sub_title'] = array(
        '#markup' => '<p>' . $plan_options[$plan_keys[0]] . '</p>'
      );
      $form['subscription']['plan_id'] = array(
        '#type' => 'hidden',
        '#value' => $plan_keys[0]
      );
    }
  }
  else {
    $form['subscription']['plan_id'] = array(
      '#type' => 'hidden',
      '#value' => '_none'
     );
  }

  if (!stripe_subscription_live()) {
    $form['card_number']['#description'] = t('In Test Mode: Use 4242424242424242 ');
    $form['card_cvc']['#description'] = t('In Test Mode: Use 123');
  }

  // Pre-populate card fields
  if (isset($customer) && is_object($customer->active_card)) {
    //$form['card_number']['#default_value'] = $customer->active_card->type . '-****-' . $customer->active_card->last4;
    $exp_month = str_pad($customer->active_card->exp_month, 2, "0", STR_PAD_LEFT);


    $form['card_expiry_month']['#default_value'] = $exp_month;
    $form['card_expiry_year']['#default_value'] = $customer->active_card->exp_year;

    //$active_card_description = t('Use the fields below to update your active card info.');

    $form['subscription']['info']['#markup'] = '<p><label>Card on file </label>' . $customer->active_card->type . ' XXXX-' . $customer->active_card->last4 . ' expires ' . $exp_month . '/' . $customer->active_card->exp_year . '</p>';
  }
  else {
    $form['card_expiry_month']['#default_value'] = '10';
    $form['card_expiry_year']['#default_value'] = '2020';
  }

  //$form['active_card']['#description'] = $active_card_description;

  // Move card fields inside fieldset
  $form['subscription']['card_number'] = $form['card_number'];
  unset($form['card_number']);
  $form['subscription']['card_cvc'] = $form['card_cvc'];
  unset($form['card_cvc']);
  $form['subscription']['card_expiry_month'] = $form['card_expiry_month'];
  unset($form['card_expiry_month']);
  $form['subscription']['card_expiry_year'] = $form['card_expiry_year'];
  unset($form['card_expiry_year']);

  $submit_text = is_object($subscription) && isset($subscription->status) ? t('Update Subscription') : t('Create Subscription');

  if (isset($form['#form_id']) && $form['#form_id'] == 'user_register_form') {
    $form['#validate'][] = 'stripe_subscription_subscription_form_validate';
  }
  else {
    $form['subscription']['card_submit'] = array(
      '#type' => 'submit',
      '#value' => $submit_text,
      '#attributes' => array(
        'class' => array('submit-button'),
      ),
    );
  }

  return $form;
}

/**
 * Validation handler for the stripe_subscription_subscription_form and user_register_form.
 * @see stripe_subscription_subscription_form
 * @see stripe_subscription_form_user_register_form_alter
 */
function stripe_subscription_subscription_form_validate($form, &$form_state) {
  // Don't process again if the stripe customer has already been set.
  if (isset($form_state['values']['stripe_customer'])) {
    return;
  }

  stripe_initialize();

  $account = NULL;
  if ($form_state['values']['uid'] > 0) {
    $account = user_load($form_state['values']['uid']);
  }

  try {
    // Add customer record
    if (empty($form_state['values']['customer_id'])) {
      // Handling new customer/subscription for either an existing account,
      // or a new account submitted from user_register_form.
      $description = '';
      if (isset($account)) {
        $description = stripe_subscription_format_description($account->uid, $account->name, $account->mail);
      }
      else {
        $description = stripe_subscription_format_description($form_state['values']['uid'], $form_state['values']['name'], $form_state['values']['mail']);
      }

      $customer = Stripe_Customer::create(array(
        'card' => $form_state['values']['stripe_token'],
        'plan' => $form_state['values']['plan_id'],
        'email' => (isset($account) ? $account->mail : $form_state['values']['mail']),
        'description' => $description)
      );
    }
    // Update customer record
    else {
      $customer = Stripe_Customer::retrieve($form_state['values']['customer_id']);

      // Update customer info
      if (isset($form_state['values']['stripe_token'])) {
        $customer->email = $account->mail;
        $customer->description = stripe_subscription_format_description($account->uid, $account->name, $account->mail);
        $customer->card = $form_state['values']['stripe_token'];
        $customer->save();
      }

      // Update the subscription if it is set.
      if ($form_state['values']['plan_id'] != '_none') {
        $customer->updateSubscription(array('prorate' => FALSE, 'plan' => $form_state['values']['plan_id']));
        $params = array('account' => $account);

        $params['stripe_customer'] = $customer;
        $params['stripe_plan'] = $customer->subscription->plan;

        $params['previous_plan_name'] = isset($form_state['values']['previous_plan_name']) ? $form_state['values']['previous_plan_name'] : NULL;
        $params['plan_changed'] = isset($params['plan_name']) && $params['plan_name'] == $params['previous_plan_name'] ? FALSE : TRUE;

        if ($form_state['values']['new_subscription']) {
          // Send new subscription confirmation
          drupal_mail('stripe_subscription', 'subscription_created', $params['account']->mail, user_preferred_language($params['account']), $params);
        }
        else {
          // Send subscription updated confirmation for plan change
          if ($params['plan_changed']) {
            drupal_mail('stripe_subscription', 'subscription_updated', $params['account']->mail, user_preferred_language($params['account']), $params);
          }
        }

      }
    }
  }
  catch (Exception $e) {
    form_set_error('', 'Unable to process card.');
    watchdog('stripe', 'Stripe_Customer::create Exception: %str', array('%str' => $e->getMessage()));
    if (user_access('administer stripe')) {
      drupal_set_message(t('Stripe_Customer::create: %str', array('%str' => $e->getMessage())), 'error');
    }
    return;
  }
  $form_state['values']['stripe_customer'] = $customer;
}

/**
 * Submit handler for the stripe_subscription_subscription_form.
 * @see stripe_subscription_subscription_form
 */
function stripe_subscription_subscription_form_submit($form, &$form_state) {
  stripe_subscription_sync_customer($form_state['values']['stripe_customer'], $form_state['values']['uid']);
  drupal_set_message('Your subscription has been updated. You will receive email conformation shortly.');
}

/**
 * Cancel Subscription Form
 */
function stripe_subscription_subscription_cancel_form($form, &$form_state, $uid = NULL) {
  stripe_initialize();
  global $user;
  if (!isset($uid)) {
    $uid = $user->uid;
  }
  else {
    if ($user->uid != $uid && !user_access('administer stripe')) {
      drupal_access_denied();
      return;
    }
  }

  $account = user_load($uid);
  $back_path = str_replace('/cancel', '', current_path());

  // Test to see if there is a customer_id for this user already
  $livemode = stripe_subscription_live() ? 1 : 0;
  $customer = NULL;
  $subscription = NULL;
  $customer_id = db_select('stripe_customers', 'sc')
    ->fields('sc', array('customer_id'))
    ->condition('sc.uid', $account->uid)
    ->condition('sc.livemode', $livemode)
    ->execute()->fetchField();

  // If the customer ID exists, request the customer
  // object to modify the subscription form.
  if (!empty($customer_id)) {
    try {
      $customer = Stripe_Customer::retrieve($customer_id);
      if (!empty($customer->subscription)) {
        $subscription = $customer->subscription;
      }
    }
    catch(Exception $e) {

    }
  }

  if (!isset($subscription)) {
    drupal_goto($back_path);
    return;
  }

  $form['customer_id'] = array(
    '#type' => 'hidden',
    '#value' => $customer_id
  );

  $form['uid'] = array(
    '#type' => 'hidden',
    '#value' => $account->uid
  );

  $form['subscription'] = array(
    '#type' => 'fieldset',
    '#title' => 'Subscription Info'
  );

  if (is_object($subscription)) {

    // Build subscription status table.
    $header = array(t('Status'), t('Current Plan'), t('Next Invoice'));
    $status = isset($subscription->status) ? ucfirst($subscription->status) : t('Inactive');
    $current = isset($subscription->plan) ? stripe_subscription_plan_name($subscription->plan) : t('None');
    $next = format_date($subscription->current_period_end+1, 'custom', 'm/d/Y');
    $rows = array(array($status, $current, $next));

    $subscription_for = '<p><label>' . t('Subscriber:') . '</label>' . $account->name . ' (' . $account->mail . ')</p>';

    $form['subscription']['status'] = array(
      '#markup' => $subscription_for . theme('table', array('header' => $header, 'rows' => $rows))
    );

    if (isset($subscription->plan)) {
      $form['previous_plan_name'] = array(
        '#type' => 'hidden',
        '#value' => $subscription->plan->name
      );
    }
  }

  $form['subscription']['cancel_copy'] = array(
    '#markup' => '<p>' . t('Are you sure you would like to cancel this subscription?') . '</p>'
  );

  $form['subscription']['cancel_link'] = array(
    '#markup' => l('No, Go back ', $back_path)
  );

  $form['subscription']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Yes, Cancel Subscription')
  );

  return $form;
}

function stripe_subscription_subscription_cancel_form_submit($form, &$form_state) {
  stripe_initialize();
  try {
    $cu = Stripe_Customer::retrieve($form_state['values']['customer_id']);
    $cu->cancelSubscription();
    drupal_set_message('Your subscription has been cancelled.');
  }
  catch(Exception $e) {
    drupal_set_message('Error cancelling subscription.');
  }
}
