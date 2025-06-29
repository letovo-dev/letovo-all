--
-- PostgreSQL database dump
--

-- Dumped from database version 16.4 (Debian 16.4-3+b1)
-- Dumped by pg_dump version 17.4

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: can_publish_as(text, text); Type: FUNCTION; Schema: public; Owner: -
--

CREATE FUNCTION public.can_publish_as(publisher_username text, target_username text) RETURNS boolean
    LANGUAGE plpgsql STABLE SECURITY DEFINER
    AS $$
DECLARE
  pub_rights TEXT;
  tgt_rights TEXT;
BEGIN
  -- достаём права первого
  SELECT userrights
    INTO pub_rights
    FROM "user"
   WHERE username = publisher_username;

  -- достаём права второго
  SELECT userrights
    INTO tgt_rights
    FROM "user"
   WHERE username = target_username;

  -- логика проверки
  IF pub_rights = 'admin' AND tgt_rights LIKE '%author%' THEN
    RETURN TRUE;
  ELSIF pub_rights = 'moder' AND tgt_rights = 'public_author' THEN
    RETURN TRUE;
  ELSE
    RETURN FALSE;
  END IF;
END;
$$;


SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: user; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public."user" (
    userid integer NOT NULL,
    username character varying(255) NOT NULL,
    passwdhash character varying(255) NOT NULL,
    userrights character varying(255) NOT NULL,
    jointime timestamp without time zone DEFAULT now(),
    avatar_pic character varying,
    active boolean DEFAULT true,
    role integer DEFAULT 0,
    balance integer DEFAULT 10000,
    registered boolean DEFAULT false,
    times_visited integer DEFAULT 1,
    author boolean DEFAULT false
);


--
-- Name: get_users_by_role(text); Type: FUNCTION; Schema: public; Owner: -
--

CREATE FUNCTION public.get_users_by_role(requester_username text) RETURNS SETOF public."user"
    LANGUAGE plpgsql STABLE
    AS $$
BEGIN
  RETURN QUERY
    SELECT u.*
    FROM "user" AS u
    JOIN "user" AS r
      ON r.username = requester_username
    WHERE
      (
        r.userrights = 'admin'
        AND u.userrights LIKE '%author%'
      )
      OR
      (
        r.userrights = 'moder'
        AND u.userrights = 'public_author'
      )
      OR
      (
        r.username = u.username
      );
END;
$$;


--
-- Name: insert_post_with_category(text, text, text, boolean); Type: FUNCTION; Schema: public; Owner: -
--

CREATE FUNCTION public.insert_post_with_category(p_post_path text, p_category_name text, p_title text, p_is_secret boolean) RETURNS integer
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE
  v_category_id INTEGER;
  v_post_id     INTEGER;
BEGIN
  -- Ищем существующий category по имени
  SELECT category
    INTO v_category_id
  FROM posts
  WHERE category_name = p_category_name
  LIMIT 1;

  -- Если не нашли — берём следующий из sequence
  IF v_category_id IS NULL THEN
    v_category_id := nextval('category_seq');
  END IF;

  -- Вставляем сам пост
  INSERT INTO posts (
    post_path,
    category,
    category_name,
    title,
    is_secret
  ) VALUES (
    p_post_path,
    v_category_id,
    p_category_name,
    p_title,
    p_is_secret
  )
  RETURNING post_id
    INTO v_post_id;

  RETURN v_post_id;
END;
$$;


--
-- Name: normalize_post_categories(); Type: FUNCTION; Schema: public; Owner: -
--

CREATE FUNCTION public.normalize_post_categories() RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    rec RECORD;
    cat_id INTEGER;
BEGIN
    FOR rec IN SELECT DISTINCT category_name FROM posts LOOP
        IF rec.category_name IS NULL OR trim(rec.category_name) = '' THEN
            CONTINUE;
        END IF;

        SELECT category_id INTO cat_id
        FROM post_category
        WHERE category_name = rec.category_name;

        IF NOT FOUND THEN
            INSERT INTO post_category (category_name) 
            VALUES (rec.category_name)
            RETURNING category_id INTO cat_id;
        END IF;

        UPDATE posts
        SET category = cat_id
        WHERE category_name = rec.category_name;

        RAISE NOTICE 'Assigned category_id: % for category_name: %', cat_id, rec.category_name;
    END LOOP;
END;
$$;


--
-- Name: achivements; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.achivements (
    achivement_id integer NOT NULL,
    achivement_pic character varying,
    achivement_name character varying NOT NULL,
    achivement_decsription character varying,
    achivement_tree integer,
    level integer DEFAULT 0,
    stages integer DEFAULT 1 NOT NULL,
    category integer,
    category_name character varying DEFAULT 'дефолт'::character varying NOT NULL,
    departmentid integer DEFAULT '-1'::integer
);


--
-- Name: achivements_achivement_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.achivements_achivement_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: achivements_achivement_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.achivements_achivement_id_seq OWNED BY public.achivements.achivement_id;


--
-- Name: achivements_blokers; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.achivements_blokers (
    id integer NOT NULL,
    parent integer NOT NULL,
    child integer NOT NULL
);


--
-- Name: achivements_blokers_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.achivements_blokers_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: achivements_blokers_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.achivements_blokers_id_seq OWNED BY public.achivements_blokers.id;


--
-- Name: active; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.active (
    activeid integer NOT NULL,
    activename character varying(255),
    activeticker character varying(16),
    activeprice integer,
    activedescription text,
    ispublic boolean
);


--
-- Name: activeHistory; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public."activeHistory" (
    dealid integer NOT NULL,
    buy boolean,
    activeid integer,
    ammount integer,
    activeprice integer,
    dealtime timestamp without time zone DEFAULT now(),
    user_name character varying(255)
);


--
-- Name: comments; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.comments (
    post_id integer NOT NULL,
    comment_id integer NOT NULL,
    comment text NOT NULL,
    username character varying NOT NULL
);


--
-- Name: comments_comment_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.comments_comment_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: comments_comment_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.comments_comment_id_seq OWNED BY public.comments.comment_id;


--
-- Name: department; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.department (
    departmentid integer NOT NULL,
    departmentname character varying
);


--
-- Name: department_departmentid_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.department_departmentid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: department_departmentid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.department_departmentid_seq OWNED BY public.department.departmentid;


--
-- Name: departments; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.departments (
    departmentid integer NOT NULL,
    departmentname character varying DEFAULT 'nowhere'::character varying NOT NULL
);


--
-- Name: departments_departmentid_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.departments_departmentid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: departments_departmentid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.departments_departmentid_seq OWNED BY public.departments.departmentid;


--
-- Name: favourite_posts; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.favourite_posts (
    id integer NOT NULL,
    username character varying NOT NULL,
    post_id integer NOT NULL
);


--
-- Name: favourite_posts_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.favourite_posts_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: favourite_posts_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.favourite_posts_id_seq OWNED BY public.favourite_posts.id;


--
-- Name: file_rights; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.file_rights (
    file_path character varying NOT NULL,
    role character varying
);


--
-- Name: files_in_posts; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.files_in_posts (
    post_id integer NOT NULL,
    file_path character varying
);


--
-- Name: filesinmessage; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.filesinmessage (
    fileid integer NOT NULL,
    infilesystem character varying(255),
    rawfile bytea
);


--
-- Name: pool; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.pool (
    buy boolean,
    userid integer,
    activeid integer,
    bidprice integer,
    ammount integer,
    bidid integer NOT NULL
);


--
-- Name: pool_bidid_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.pool_bidid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: pool_bidid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.pool_bidid_seq OWNED BY public.pool.bidid;


--
-- Name: post_category; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.post_category (
    category_id integer NOT NULL,
    category_name character varying NOT NULL
);


--
-- Name: post_category_category_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.post_category_category_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: post_category_category_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.post_category_category_id_seq OWNED BY public.post_category.category_id;


--
-- Name: post_media; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.post_media (
    post_id character varying NOT NULL,
    media character varying,
    is_pic boolean,
    is_secret boolean DEFAULT false
);


--
-- Name: posts; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.posts (
    post_id integer NOT NULL,
    post_path character varying,
    is_secret boolean DEFAULT false,
    likes integer DEFAULT 0,
    title character varying DEFAULT '-'::character varying,
    author character varying,
    text text,
    dislikes integer DEFAULT 0,
    parent_id integer,
    date timestamp without time zone DEFAULT now(),
    saved_count integer DEFAULT 0,
    category integer DEFAULT 0,
    category_name character varying DEFAULT '-'::character varying
);


--
-- Name: posts_category_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.posts_category_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: posts_post_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.posts_post_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: posts_post_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.posts_post_id_seq OWNED BY public.posts.post_id;


--
-- Name: roles; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.roles (
    roleid integer NOT NULL,
    rolename character varying,
    rang integer DEFAULT 0 NOT NULL,
    departmentid integer DEFAULT 0,
    payment integer DEFAULT 0
);


--
-- Name: roles_roleid_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.roles_roleid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: roles_roleid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.roles_roleid_seq OWNED BY public.roles.roleid;


--
-- Name: transactions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.transactions (
    transactionid integer NOT NULL,
    amount bigint NOT NULL,
    sender character varying NOT NULL,
    receiver character varying NOT NULL,
    transactiontime timestamp without time zone DEFAULT now() NOT NULL
);


--
-- Name: transactions_transactionid_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.transactions_transactionid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: transactions_transactionid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.transactions_transactionid_seq OWNED BY public.transactions.transactionid;


--
-- Name: user_achivements; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.user_achivements (
    id integer NOT NULL,
    username character varying NOT NULL,
    achivement_id integer NOT NULL,
    datetime timestamp without time zone DEFAULT now() NOT NULL,
    stage integer DEFAULT 0 NOT NULL
);


--
-- Name: user_achivements_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.user_achivements_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: user_achivements_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.user_achivements_id_seq OWNED BY public.user_achivements.id;


--
-- Name: user_likes; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.user_likes (
    username character varying NOT NULL,
    post_id integer NOT NULL,
    value integer NOT NULL
);


--
-- Name: user_saved; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.user_saved (
    username character varying NOT NULL,
    post_id integer NOT NULL
);


--
-- Name: usergroup; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.usergroup (
    groupid integer NOT NULL,
    groupname character varying(255) NOT NULL,
    createtime timestamp with time zone DEFAULT now()
);


--
-- Name: usermessage; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.usermessage (
    messageid integer NOT NULL,
    messagetext text,
    messagetime timestamp without time zone DEFAULT now(),
    contenttype character varying(255),
    fileinmessage integer,
    userid integer,
    droupid integer,
    linkid integer
);


--
-- Name: useroles; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.useroles (
    username character varying NOT NULL,
    roleid integer DEFAULT 0 NOT NULL
);


--
-- Name: usersactives; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.usersactives (
    lineid integer NOT NULL,
    activeid integer,
    ammount integer,
    avgboughtprice real,
    user_name character varying(255)
);


--
-- Name: usertogroup; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.usertogroup (
    linkid integer NOT NULL,
    userid integer,
    groupid integer,
    jointime timestamp without time zone DEFAULT now(),
    adminjoined integer
);


--
-- Name: achivements achivement_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.achivements ALTER COLUMN achivement_id SET DEFAULT nextval('public.achivements_achivement_id_seq'::regclass);


--
-- Name: achivements_blokers id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.achivements_blokers ALTER COLUMN id SET DEFAULT nextval('public.achivements_blokers_id_seq'::regclass);


--
-- Name: comments comment_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.comments ALTER COLUMN comment_id SET DEFAULT nextval('public.comments_comment_id_seq'::regclass);


--
-- Name: department departmentid; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.department ALTER COLUMN departmentid SET DEFAULT nextval('public.department_departmentid_seq'::regclass);


--
-- Name: departments departmentid; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.departments ALTER COLUMN departmentid SET DEFAULT nextval('public.departments_departmentid_seq'::regclass);


--
-- Name: favourite_posts id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.favourite_posts ALTER COLUMN id SET DEFAULT nextval('public.favourite_posts_id_seq'::regclass);


--
-- Name: pool bidid; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.pool ALTER COLUMN bidid SET DEFAULT nextval('public.pool_bidid_seq'::regclass);


--
-- Name: post_category category_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.post_category ALTER COLUMN category_id SET DEFAULT nextval('public.post_category_category_id_seq'::regclass);


--
-- Name: posts post_id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.posts ALTER COLUMN post_id SET DEFAULT nextval('public.posts_post_id_seq'::regclass);


--
-- Name: roles roleid; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.roles ALTER COLUMN roleid SET DEFAULT nextval('public.roles_roleid_seq'::regclass);


--
-- Name: transactions transactionid; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.transactions ALTER COLUMN transactionid SET DEFAULT nextval('public.transactions_transactionid_seq'::regclass);


--
-- Name: user_achivements id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_achivements ALTER COLUMN id SET DEFAULT nextval('public.user_achivements_id_seq'::regclass);


--
-- Name: achivements_blokers achivements_blokers_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.achivements_blokers
    ADD CONSTRAINT achivements_blokers_unique UNIQUE (id);


--
-- Name: achivements achivements_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.achivements
    ADD CONSTRAINT achivements_unique UNIQUE (achivement_id);


--
-- Name: activeHistory activeHistory_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public."activeHistory"
    ADD CONSTRAINT "activeHistory_pkey" PRIMARY KEY (dealid);


--
-- Name: active active_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.active
    ADD CONSTRAINT active_pkey PRIMARY KEY (activeid);


--
-- Name: department department_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.department
    ADD CONSTRAINT department_unique UNIQUE (departmentid);


--
-- Name: favourite_posts favourite_posts_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.favourite_posts
    ADD CONSTRAINT favourite_posts_unique UNIQUE (id);


--
-- Name: files_in_posts files_in_posts_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.files_in_posts
    ADD CONSTRAINT files_in_posts_unique UNIQUE (post_id, file_path);


--
-- Name: filesinmessage filesinmessage_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.filesinmessage
    ADD CONSTRAINT filesinmessage_pkey PRIMARY KEY (fileid);


--
-- Name: pool pool_pk; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.pool
    ADD CONSTRAINT pool_pk PRIMARY KEY (bidid);


--
-- Name: post_category post_category_pk; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.post_category
    ADD CONSTRAINT post_category_pk PRIMARY KEY (category_id);


--
-- Name: post_category post_category_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.post_category
    ADD CONSTRAINT post_category_unique UNIQUE (category_name);


--
-- Name: posts post_id; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.posts
    ADD CONSTRAINT post_id PRIMARY KEY (post_id);


--
-- Name: roles roles_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.roles
    ADD CONSTRAINT roles_unique UNIQUE (roleid);


--
-- Name: roles roles_unique_1; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.roles
    ADD CONSTRAINT roles_unique_1 UNIQUE (rolename, departmentid);


--
-- Name: user unuque_username; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public."user"
    ADD CONSTRAINT unuque_username UNIQUE (username);


--
-- Name: user_achivements user_achivements_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_unique UNIQUE (id);


--
-- Name: user_achivements user_achivements_unique_1; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_unique_1 UNIQUE (username, achivement_id);


--
-- Name: user_likes user_likes_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_likes
    ADD CONSTRAINT user_likes_unique UNIQUE (username, post_id);


--
-- Name: user user_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public."user"
    ADD CONSTRAINT user_pkey PRIMARY KEY (userid);


--
-- Name: usergroup usergroup_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usergroup
    ADD CONSTRAINT usergroup_pkey PRIMARY KEY (groupid);


--
-- Name: usermessage usermessage_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_pkey PRIMARY KEY (messageid);


--
-- Name: usersactives usersactives_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usersactives
    ADD CONSTRAINT usersactives_pkey PRIMARY KEY (lineid);


--
-- Name: usertogroup usertogroup_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usertogroup
    ADD CONSTRAINT usertogroup_pkey PRIMARY KEY (linkid);


--
-- Name: idx_favourite_posts; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX idx_favourite_posts ON public.favourite_posts USING btree (post_id, username);


--
-- Name: achivements_blokers achivements_blokers_achivements_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.achivements_blokers
    ADD CONSTRAINT achivements_blokers_achivements_fk FOREIGN KEY (parent) REFERENCES public.achivements(achivement_id);


--
-- Name: achivements_blokers achivements_blokers_achivements_fk_1; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.achivements_blokers
    ADD CONSTRAINT achivements_blokers_achivements_fk_1 FOREIGN KEY (child) REFERENCES public.achivements(achivement_id);


--
-- Name: activeHistory activeHistory_activeid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public."activeHistory"
    ADD CONSTRAINT "activeHistory_activeid_fkey" FOREIGN KEY (activeid) REFERENCES public.active(activeid);


--
-- Name: pool pool_activeid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.pool
    ADD CONSTRAINT pool_activeid_fkey FOREIGN KEY (activeid) REFERENCES public.active(activeid);


--
-- Name: pool pool_userid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.pool
    ADD CONSTRAINT pool_userid_fkey FOREIGN KEY (userid) REFERENCES public."user"(userid);


--
-- Name: roles roles_department_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.roles
    ADD CONSTRAINT roles_department_fk FOREIGN KEY (departmentid) REFERENCES public.department(departmentid);


--
-- Name: transactions transactions_user_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.transactions
    ADD CONSTRAINT transactions_user_fk FOREIGN KEY (sender) REFERENCES public."user"(username) ON UPDATE CASCADE;


--
-- Name: transactions transactions_user_fk_1; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.transactions
    ADD CONSTRAINT transactions_user_fk_1 FOREIGN KEY (receiver) REFERENCES public."user"(username) ON UPDATE CASCADE;


--
-- Name: user_achivements user_achivements_achivements_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_achivements_fk FOREIGN KEY (achivement_id) REFERENCES public.achivements(achivement_id);


--
-- Name: user_achivements user_achivements_user_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_user_fk FOREIGN KEY (username) REFERENCES public."user"(username) ON UPDATE CASCADE;


--
-- Name: user user_roles_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public."user"
    ADD CONSTRAINT user_roles_fk FOREIGN KEY (role) REFERENCES public.roles(roleid);


--
-- Name: usermessage usermessage_droupid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_droupid_fkey FOREIGN KEY (droupid) REFERENCES public.usergroup(groupid);


--
-- Name: usermessage usermessage_fileinmessage_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_fileinmessage_fkey FOREIGN KEY (fileinmessage) REFERENCES public.filesinmessage(fileid);


--
-- Name: usermessage usermessage_linkid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_linkid_fkey FOREIGN KEY (linkid) REFERENCES public.usertogroup(linkid);


--
-- Name: usermessage usermessage_userid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_userid_fkey FOREIGN KEY (userid) REFERENCES public."user"(userid);


--
-- Name: useroles useroles_roles_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.useroles
    ADD CONSTRAINT useroles_roles_fk FOREIGN KEY (roleid) REFERENCES public.roles(roleid);


--
-- Name: useroles useroles_user_fk; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.useroles
    ADD CONSTRAINT useroles_user_fk FOREIGN KEY (username) REFERENCES public."user"(username) ON UPDATE CASCADE;


--
-- Name: usersactives usersactives_activeid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usersactives
    ADD CONSTRAINT usersactives_activeid_fkey FOREIGN KEY (activeid) REFERENCES public.active(activeid);


--
-- Name: usertogroup usertogroup_groupid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usertogroup
    ADD CONSTRAINT usertogroup_groupid_fkey FOREIGN KEY (groupid) REFERENCES public.usergroup(groupid);


--
-- Name: usertogroup usertogroup_userid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.usertogroup
    ADD CONSTRAINT usertogroup_userid_fkey FOREIGN KEY (userid) REFERENCES public."user"(userid);


--
-- PostgreSQL database dump complete
--

