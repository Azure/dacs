//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
#pragma once
#include<type_traits>
//--------------------------------------------------------------------------------------------------------------------------------------------
#define TYPE_HELPER \
    static auto helper() -> std::remove_reference<decltype(*this)>::type; \
    using this_type = decltype(helper())
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotCopyConstructible
{
    TYPE_HELPER;
    NotCopyConstructible() = default;
    NotCopyConstructible(this_type&&) = default;
    this_type& operator=(const this_type&) = default;
    this_type& operator=(this_type&&) = default;

    NotCopyConstructible(const this_type&) = delete;

    static_assert(!std::is_copy_constructible_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotMoveConstructible
{
    TYPE_HELPER;
    NotMoveConstructible() = default;
    NotMoveConstructible(const this_type&) = default;
    this_type& operator=(const this_type&) = default;
    this_type& operator=(this_type&&) = default;

    NotMoveConstructible(this_type&&) = delete;

    static_assert(!std::is_move_constructible_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotCopyAssignable
{
    TYPE_HELPER;
    NotCopyAssignable() = default;
    NotCopyAssignable(const this_type&) = default;
    NotCopyAssignable(this_type&&) = default;
    this_type& operator=(this_type&&) = default;

    this_type& operator=(const this_type&) = delete;

    static_assert(!std::is_copy_assignable_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotMoveAssignable
{
    TYPE_HELPER;
    NotMoveAssignable() = default;
    NotMoveAssignable(const this_type&) = default;
    NotMoveAssignable(this_type&&) = default;
    this_type& operator=(const this_type&) = default;

    this_type& operator=(this_type&&) = delete;

    static_assert(!std::is_move_assignable_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotAssignable : public NotCopyAssignable, public NotMoveAssignable
{
    TYPE_HELPER;
    static_assert(!std::is_copy_assignable_v<this_type>);
    static_assert(!std::is_move_assignable_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotConstructible : public NotCopyConstructible, public NotMoveConstructible
{
    TYPE_HELPER;
    static_assert(!std::is_copy_constructible_v<this_type>);
    static_assert(!std::is_move_constructible_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotCopyable : public NotCopyAssignable, public NotCopyConstructible
{
    TYPE_HELPER;
    static_assert(!std::is_copy_assignable_v<this_type>);
    static_assert(!std::is_copy_constructible_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotMovable : public NotMoveAssignable, public NotMoveConstructible
{
    TYPE_HELPER;
    static_assert(!std::is_move_assignable_v<this_type>);
    static_assert(!std::is_move_constructible_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
struct NotCopyableMovable : public NotCopyable, public NotMovable
{
    TYPE_HELPER;
    static_assert(!std::is_copy_assignable_v<this_type>);
    static_assert(!std::is_move_assignable_v<this_type>);
    static_assert(!std::is_copy_constructible_v<this_type>);
    static_assert(!std::is_move_constructible_v<this_type>);
};
//--------------------------------------------------------------------------------------------------------------------------------------------
static_assert(std::is_trivially_default_constructible_v<NotCopyConstructible>);
static_assert(std::is_trivially_default_constructible_v<NotMoveConstructible>);
static_assert(std::is_trivially_default_constructible_v<NotCopyAssignable>);
static_assert(std::is_trivially_default_constructible_v<NotMoveAssignable>);
static_assert(std::is_default_constructible_v<NotAssignable>);
static_assert(std::is_default_constructible_v<NotConstructible>);
static_assert(std::is_default_constructible_v<NotCopyable>);
static_assert(std::is_default_constructible_v<NotMovable>);
static_assert(std::is_default_constructible_v<NotCopyableMovable>);
//--------------------------------------------------------------------------------------------------------------------------------------------
