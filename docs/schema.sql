--
-- PostgreSQL database dump
--

\restrict DcZgZcQEi4f3bvxPbX2ujwdl1hqpF6E4scmb22IrRXAOOfLe0MsHhy1DhNbFZEJ

-- Dumped from database version 16.11 (Debian 16.11-1.pgdg13+1)
-- Dumped by pg_dump version 16.11 (Debian 16.11-1.pgdg13+1)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: can_publish_as(text, text); Type: FUNCTION; Schema: public; Owner: scv
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


ALTER FUNCTION public.can_publish_as(publisher_username text, target_username text) OWNER TO scv;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: user; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public."user" (
    userid integer NOT NULL,
    username character varying(255) NOT NULL,
    display_name text,
    passwdhash character varying(255) NOT NULL,
    userrights character varying(255),
    jointime timestamp without time zone DEFAULT now(),
    avatar_pic character varying,
    active boolean DEFAULT true,
    role integer DEFAULT 0,
    balance integer DEFAULT 0,
    registered boolean DEFAULT true,
    times_visited integer DEFAULT 1,
    author boolean DEFAULT false,
    chattable boolean DEFAULT false
);


ALTER TABLE public."user" OWNER TO scv;

--
-- Name: get_users_by_role(text); Type: FUNCTION; Schema: public; Owner: scv
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


ALTER FUNCTION public.get_users_by_role(requester_username text) OWNER TO scv;

--
-- Name: insert_post_with_category(text, text, text, boolean); Type: FUNCTION; Schema: public; Owner: scv
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


ALTER FUNCTION public.insert_post_with_category(p_post_path text, p_category_name text, p_title text, p_is_secret boolean) OWNER TO scv;

--
-- Name: normalize_post_categories(); Type: FUNCTION; Schema: public; Owner: scv
--

CREATE FUNCTION public.normalize_post_categories() RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    rec RECORD;
    cat_id INTEGER;
BEGIN
update posts set post_path = null where post_path = '';
END;
$$;


ALTER FUNCTION public.normalize_post_categories() OWNER TO scv;

--
-- Name: achivements; Type: TABLE; Schema: public; Owner: scv
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


ALTER TABLE public.achivements OWNER TO scv;

--
-- Name: achivements_achivement_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.achivements_achivement_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.achivements_achivement_id_seq OWNER TO scv;

--
-- Name: achivements_achivement_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.achivements_achivement_id_seq OWNED BY public.achivements.achivement_id;


--
-- Name: achivements_blokers; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.achivements_blokers (
    id integer NOT NULL,
    parent integer NOT NULL,
    child integer NOT NULL
);


ALTER TABLE public.achivements_blokers OWNER TO scv;

--
-- Name: achivements_blokers_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.achivements_blokers_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.achivements_blokers_id_seq OWNER TO scv;

--
-- Name: achivements_blokers_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.achivements_blokers_id_seq OWNED BY public.achivements_blokers.id;


--
-- Name: active; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.active (
    activeid integer NOT NULL,
    activename character varying(255),
    activeticker character varying(16),
    activeprice integer,
    activedescription text,
    ispublic boolean
);


ALTER TABLE public.active OWNER TO scv;

--
-- Name: activeHistory; Type: TABLE; Schema: public; Owner: scv
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


ALTER TABLE public."activeHistory" OWNER TO scv;

--
-- Name: comments; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.comments (
    post_id integer NOT NULL,
    comment_id integer NOT NULL,
    comment text NOT NULL,
    username character varying NOT NULL
);


ALTER TABLE public.comments OWNER TO scv;

--
-- Name: comments_comment_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.comments_comment_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.comments_comment_id_seq OWNER TO scv;

--
-- Name: comments_comment_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.comments_comment_id_seq OWNED BY public.comments.comment_id;


--
-- Name: cookies; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.cookies (
    cookie_id integer NOT NULL,
    username character varying,
    cookie character varying,
    createted timestamp without time zone DEFAULT now(),
    updated timestamp without time zone DEFAULT now(),
    useragent character varying
);


ALTER TABLE public.cookies OWNER TO scv;

--
-- Name: cookies_cookie_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.cookies_cookie_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.cookies_cookie_id_seq OWNER TO scv;

--
-- Name: cookies_cookie_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.cookies_cookie_id_seq OWNED BY public.cookies.cookie_id;


--
-- Name: department; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.department (
    departmentid integer NOT NULL,
    departmentname character varying
);


ALTER TABLE public.department OWNER TO scv;

--
-- Name: department_departmentid_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.department_departmentid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.department_departmentid_seq OWNER TO scv;

--
-- Name: department_departmentid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.department_departmentid_seq OWNED BY public.department.departmentid;


--
-- Name: departments; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.departments (
    departmentid integer NOT NULL,
    departmentname character varying DEFAULT 'nowhere'::character varying NOT NULL
);


ALTER TABLE public.departments OWNER TO scv;

--
-- Name: departments_departmentid_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.departments_departmentid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.departments_departmentid_seq OWNER TO scv;

--
-- Name: departments_departmentid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.departments_departmentid_seq OWNED BY public.departments.departmentid;


--
-- Name: favourite_posts; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.favourite_posts (
    id integer NOT NULL,
    username character varying NOT NULL,
    post_id integer NOT NULL
);


ALTER TABLE public.favourite_posts OWNER TO scv;

--
-- Name: favourite_posts_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.favourite_posts_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.favourite_posts_id_seq OWNER TO scv;

--
-- Name: favourite_posts_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.favourite_posts_id_seq OWNED BY public.favourite_posts.id;


--
-- Name: file_rights; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.file_rights (
    file_path character varying NOT NULL,
    role character varying
);


ALTER TABLE public.file_rights OWNER TO scv;

--
-- Name: files_in_posts; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.files_in_posts (
    post_id integer NOT NULL,
    file_path character varying
);


ALTER TABLE public.files_in_posts OWNER TO scv;

--
-- Name: filesinmessage; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.filesinmessage (
    fileid integer NOT NULL,
    infilesystem character varying(255),
    rawfile bytea
);


ALTER TABLE public.filesinmessage OWNER TO scv;

--
-- Name: mytable_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.mytable_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.mytable_id_seq OWNER TO scv;

--
-- Name: mytable_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.mytable_id_seq OWNED BY public."user".userid;


--
-- Name: pool; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.pool (
    buy boolean,
    userid integer,
    activeid integer,
    bidprice integer,
    ammount integer,
    bidid integer NOT NULL
);


ALTER TABLE public.pool OWNER TO scv;

--
-- Name: pool_bidid_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.pool_bidid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.pool_bidid_seq OWNER TO scv;

--
-- Name: pool_bidid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.pool_bidid_seq OWNED BY public.pool.bidid;


--
-- Name: post_category; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.post_category (
    category_id integer NOT NULL,
    category_name character varying NOT NULL
);


ALTER TABLE public.post_category OWNER TO scv;

--
-- Name: post_category_category_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.post_category_category_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.post_category_category_id_seq OWNER TO scv;

--
-- Name: post_category_category_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.post_category_category_id_seq OWNED BY public.post_category.category_id;


--
-- Name: post_media; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.post_media (
    post_id character varying NOT NULL,
    media character varying,
    is_pic boolean,
    is_secret boolean DEFAULT false
);


ALTER TABLE public.post_media OWNER TO scv;

--
-- Name: posts; Type: TABLE; Schema: public; Owner: scv
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


ALTER TABLE public.posts OWNER TO scv;

--
-- Name: posts_category_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.posts_category_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.posts_category_seq OWNER TO scv;

--
-- Name: posts_post_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.posts_post_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.posts_post_id_seq OWNER TO scv;

--
-- Name: posts_post_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.posts_post_id_seq OWNED BY public.posts.post_id;


--
-- Name: role; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.role (
    username character varying NOT NULL,
    write_posts boolean,
    admin boolean,
    moder boolean,
    main_page boolean
);


ALTER TABLE public.role OWNER TO scv;

--
-- Name: roles; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.roles (
    roleid integer NOT NULL,
    rolename character varying,
    rang integer DEFAULT 0 NOT NULL,
    departmentid integer DEFAULT 0,
    payment integer DEFAULT 0
);


ALTER TABLE public.roles OWNER TO scv;

--
-- Name: roles_roleid_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.roles_roleid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.roles_roleid_seq OWNER TO scv;

--
-- Name: roles_roleid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.roles_roleid_seq OWNED BY public.roles.roleid;


--
-- Name: transactions; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.transactions (
    transactionid integer NOT NULL,
    amount bigint NOT NULL,
    sender character varying,
    receiver character varying,
    transactiontime timestamp without time zone DEFAULT now() NOT NULL
);


ALTER TABLE public.transactions OWNER TO scv;

--
-- Name: suspicious_transactions; Type: VIEW; Schema: public; Owner: scv
--

CREATE VIEW public.suspicious_transactions AS
 SELECT DISTINCT t.transactionid,
    t.amount,
    t.sender,
    t.receiver,
    t.transactiontime,
    c.cookie AS device_owner,
    c.username AS login_user,
    c.createted AS login_time,
    c.useragent AS user_device_info
   FROM ((public.transactions t
     LEFT JOIN public.cookies c ON ((((t.receiver)::text = (c.cookie)::text) AND ((t.sender)::text = (c.username)::text))))
     LEFT JOIN public."user" us ON (((us.username)::text = (t.sender)::text)))
  WHERE ((t.transactiontime > c.createted) AND ((us.userrights)::text <> 'admin'::text));


ALTER VIEW public.suspicious_transactions OWNER TO scv;

--
-- Name: transactions_transactionid_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.transactions_transactionid_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.transactions_transactionid_seq OWNER TO scv;

--
-- Name: transactions_transactionid_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.transactions_transactionid_seq OWNED BY public.transactions.transactionid;


--
-- Name: user_achivements; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.user_achivements (
    id integer NOT NULL,
    username character varying NOT NULL,
    achivement_id integer NOT NULL,
    datetime timestamp without time zone DEFAULT now() NOT NULL,
    stage integer DEFAULT 0 NOT NULL
);


ALTER TABLE public.user_achivements OWNER TO scv;

--
-- Name: user_achivements_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.user_achivements_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.user_achivements_id_seq OWNER TO scv;

--
-- Name: user_achivements_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: scv
--

ALTER SEQUENCE public.user_achivements_id_seq OWNED BY public.user_achivements.id;


--
-- Name: user_device_counts; Type: VIEW; Schema: public; Owner: scv
--

CREATE VIEW public.user_device_counts AS
 SELECT username,
    count(DISTINCT cookie) AS device_count,
    (count(DISTINCT cookie) > 1) AS maybe_hacked
   FROM public.cookies
  WHERE (((username)::text <> 'admin'::text) AND ((cookie)::text <> 'admin'::text))
  GROUP BY username
  ORDER BY (count(DISTINCT cookie)) DESC, username;


ALTER VIEW public.user_device_counts OWNER TO scv;

--
-- Name: user_likes; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.user_likes (
    username character varying NOT NULL,
    post_id integer NOT NULL,
    value integer NOT NULL
);


ALTER TABLE public.user_likes OWNER TO scv;

--
-- Name: user_saved; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.user_saved (
    username character varying NOT NULL,
    post_id integer NOT NULL
);


ALTER TABLE public.user_saved OWNER TO scv;

--
-- Name: user_view; Type: VIEW; Schema: public; Owner: scv
--

CREATE VIEW public.user_view AS
 SELECT username,
    userrights,
    role,
    avatar_pic
   FROM public."user";


ALTER VIEW public.user_view OWNER TO scv;

--
-- Name: usergroup; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.usergroup (
    groupid integer NOT NULL,
    groupname character varying(255) NOT NULL,
    createtime timestamp with time zone DEFAULT now()
);


ALTER TABLE public.usergroup OWNER TO scv;

--
-- Name: usermessage; Type: TABLE; Schema: public; Owner: scv
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


ALTER TABLE public.usermessage OWNER TO scv;

--
-- Name: useroles; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.useroles (
    username character varying NOT NULL,
    roleid integer DEFAULT 0 NOT NULL
);


ALTER TABLE public.useroles OWNER TO scv;

--
-- Name: usersactives; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.usersactives (
    lineid integer NOT NULL,
    activeid integer,
    ammount integer,
    avgboughtprice real,
    user_name character varying(255)
);


ALTER TABLE public.usersactives OWNER TO scv;

--
-- Name: usertogroup; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.usertogroup (
    linkid integer NOT NULL,
    userid integer,
    groupid integer,
    jointime timestamp without time zone DEFAULT now(),
    adminjoined integer
);


ALTER TABLE public.usertogroup OWNER TO scv;

--
-- Name: achivements achivement_id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.achivements ALTER COLUMN achivement_id SET DEFAULT nextval('public.achivements_achivement_id_seq'::regclass);


--
-- Name: achivements_blokers id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.achivements_blokers ALTER COLUMN id SET DEFAULT nextval('public.achivements_blokers_id_seq'::regclass);


--
-- Name: comments comment_id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.comments ALTER COLUMN comment_id SET DEFAULT nextval('public.comments_comment_id_seq'::regclass);


--
-- Name: cookies cookie_id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.cookies ALTER COLUMN cookie_id SET DEFAULT nextval('public.cookies_cookie_id_seq'::regclass);


--
-- Name: department departmentid; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.department ALTER COLUMN departmentid SET DEFAULT nextval('public.department_departmentid_seq'::regclass);


--
-- Name: departments departmentid; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.departments ALTER COLUMN departmentid SET DEFAULT nextval('public.departments_departmentid_seq'::regclass);


--
-- Name: favourite_posts id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.favourite_posts ALTER COLUMN id SET DEFAULT nextval('public.favourite_posts_id_seq'::regclass);


--
-- Name: pool bidid; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.pool ALTER COLUMN bidid SET DEFAULT nextval('public.pool_bidid_seq'::regclass);


--
-- Name: post_category category_id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.post_category ALTER COLUMN category_id SET DEFAULT nextval('public.post_category_category_id_seq'::regclass);


--
-- Name: posts post_id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.posts ALTER COLUMN post_id SET DEFAULT nextval('public.posts_post_id_seq'::regclass);


--
-- Name: roles roleid; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.roles ALTER COLUMN roleid SET DEFAULT nextval('public.roles_roleid_seq'::regclass);


--
-- Name: transactions transactionid; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.transactions ALTER COLUMN transactionid SET DEFAULT nextval('public.transactions_transactionid_seq'::regclass);


--
-- Name: user userid; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public."user" ALTER COLUMN userid SET DEFAULT nextval('public.mytable_id_seq'::regclass);


--
-- Name: user_achivements id; Type: DEFAULT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.user_achivements ALTER COLUMN id SET DEFAULT nextval('public.user_achivements_id_seq'::regclass);


--
-- Name: achivements_blokers achivements_blokers_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.achivements_blokers
    ADD CONSTRAINT achivements_blokers_unique UNIQUE (id);


--
-- Name: achivements achivements_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.achivements
    ADD CONSTRAINT achivements_unique UNIQUE (achivement_id);


--
-- Name: activeHistory activeHistory_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public."activeHistory"
    ADD CONSTRAINT "activeHistory_pkey" PRIMARY KEY (dealid);


--
-- Name: active active_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.active
    ADD CONSTRAINT active_pkey PRIMARY KEY (activeid);


--
-- Name: cookies cookies_pk; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.cookies
    ADD CONSTRAINT cookies_pk PRIMARY KEY (cookie_id);


--
-- Name: department department_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.department
    ADD CONSTRAINT department_unique UNIQUE (departmentid);


--
-- Name: favourite_posts favourite_posts_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.favourite_posts
    ADD CONSTRAINT favourite_posts_unique UNIQUE (id);


--
-- Name: files_in_posts files_in_posts_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.files_in_posts
    ADD CONSTRAINT files_in_posts_unique UNIQUE (post_id, file_path);


--
-- Name: filesinmessage filesinmessage_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.filesinmessage
    ADD CONSTRAINT filesinmessage_pkey PRIMARY KEY (fileid);


--
-- Name: pool pool_pk; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.pool
    ADD CONSTRAINT pool_pk PRIMARY KEY (bidid);


--
-- Name: post_category post_category_pk; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.post_category
    ADD CONSTRAINT post_category_pk PRIMARY KEY (category_id);


--
-- Name: post_category post_category_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.post_category
    ADD CONSTRAINT post_category_unique UNIQUE (category_name);


--
-- Name: posts post_id; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.posts
    ADD CONSTRAINT post_id PRIMARY KEY (post_id);


--
-- Name: role role_pk; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.role
    ADD CONSTRAINT role_pk PRIMARY KEY (username);


--
-- Name: roles roles_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.roles
    ADD CONSTRAINT roles_unique UNIQUE (roleid);


--
-- Name: user unuque_username; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public."user"
    ADD CONSTRAINT unuque_username UNIQUE (username);


--
-- Name: user_achivements user_achivements_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_unique UNIQUE (id);


--
-- Name: user_achivements user_achivements_unique_1; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_unique_1 UNIQUE (username, achivement_id);


--
-- Name: user_likes user_likes_unique; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.user_likes
    ADD CONSTRAINT user_likes_unique UNIQUE (username, post_id);


--
-- Name: user user_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public."user"
    ADD CONSTRAINT user_pkey PRIMARY KEY (userid);


--
-- Name: usergroup usergroup_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usergroup
    ADD CONSTRAINT usergroup_pkey PRIMARY KEY (groupid);


--
-- Name: usermessage usermessage_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_pkey PRIMARY KEY (messageid);


--
-- Name: usersactives usersactives_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usersactives
    ADD CONSTRAINT usersactives_pkey PRIMARY KEY (lineid);


--
-- Name: usertogroup usertogroup_pkey; Type: CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usertogroup
    ADD CONSTRAINT usertogroup_pkey PRIMARY KEY (linkid);


--
-- Name: idx_favourite_posts; Type: INDEX; Schema: public; Owner: scv
--

CREATE UNIQUE INDEX idx_favourite_posts ON public.favourite_posts USING btree (post_id, username);


--
-- Name: achivements_blokers achivements_blokers_achivements_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.achivements_blokers
    ADD CONSTRAINT achivements_blokers_achivements_fk FOREIGN KEY (parent) REFERENCES public.achivements(achivement_id);


--
-- Name: achivements_blokers achivements_blokers_achivements_fk_1; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.achivements_blokers
    ADD CONSTRAINT achivements_blokers_achivements_fk_1 FOREIGN KEY (child) REFERENCES public.achivements(achivement_id);


--
-- Name: activeHistory activeHistory_activeid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public."activeHistory"
    ADD CONSTRAINT "activeHistory_activeid_fkey" FOREIGN KEY (activeid) REFERENCES public.active(activeid);


--
-- Name: pool pool_activeid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.pool
    ADD CONSTRAINT pool_activeid_fkey FOREIGN KEY (activeid) REFERENCES public.active(activeid);


--
-- Name: pool pool_userid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.pool
    ADD CONSTRAINT pool_userid_fkey FOREIGN KEY (userid) REFERENCES public."user"(userid);


--
-- Name: posts posts_post_category_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.posts
    ADD CONSTRAINT posts_post_category_fk FOREIGN KEY (category) REFERENCES public.post_category(category_id);


--
-- Name: role role_user_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.role
    ADD CONSTRAINT role_user_fk FOREIGN KEY (username) REFERENCES public."user"(username) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: roles roles_department_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.roles
    ADD CONSTRAINT roles_department_fk FOREIGN KEY (departmentid) REFERENCES public.department(departmentid);


--
-- Name: transactions transactions_user_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.transactions
    ADD CONSTRAINT transactions_user_fk FOREIGN KEY (sender) REFERENCES public."user"(username) ON UPDATE SET NULL ON DELETE SET NULL;


--
-- Name: transactions transactions_user_fk_1; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.transactions
    ADD CONSTRAINT transactions_user_fk_1 FOREIGN KEY (receiver) REFERENCES public."user"(username) ON UPDATE SET NULL ON DELETE SET NULL;


--
-- Name: user_achivements user_achivements_achivements_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_achivements_fk FOREIGN KEY (achivement_id) REFERENCES public.achivements(achivement_id);


--
-- Name: user_achivements user_achivements_user_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.user_achivements
    ADD CONSTRAINT user_achivements_user_fk FOREIGN KEY (username) REFERENCES public."user"(username) ON UPDATE CASCADE;


--
-- Name: user user_roles_fk; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public."user"
    ADD CONSTRAINT user_roles_fk FOREIGN KEY (role) REFERENCES public.roles(roleid);


--
-- Name: usermessage usermessage_droupid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_droupid_fkey FOREIGN KEY (droupid) REFERENCES public.usergroup(groupid);


--
-- Name: usermessage usermessage_fileinmessage_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_fileinmessage_fkey FOREIGN KEY (fileinmessage) REFERENCES public.filesinmessage(fileid);


--
-- Name: usermessage usermessage_linkid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_linkid_fkey FOREIGN KEY (linkid) REFERENCES public.usertogroup(linkid);


--
-- Name: usermessage usermessage_userid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usermessage
    ADD CONSTRAINT usermessage_userid_fkey FOREIGN KEY (userid) REFERENCES public."user"(userid);


--
-- Name: usersactives usersactives_activeid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usersactives
    ADD CONSTRAINT usersactives_activeid_fkey FOREIGN KEY (activeid) REFERENCES public.active(activeid);


--
-- Name: usertogroup usertogroup_groupid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usertogroup
    ADD CONSTRAINT usertogroup_groupid_fkey FOREIGN KEY (groupid) REFERENCES public.usergroup(groupid);


--
-- Name: usertogroup usertogroup_userid_fkey; Type: FK CONSTRAINT; Schema: public; Owner: scv
--

ALTER TABLE ONLY public.usertogroup
    ADD CONSTRAINT usertogroup_userid_fkey FOREIGN KEY (userid) REFERENCES public."user"(userid);


--
-- Name: direct_message; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.direct_message (
    message_id integer NOT NULL,
    sender character varying(255) NOT NULL,
    receiver character varying(255) NOT NULL,
    message_text text,
    sent_at timestamp without time zone DEFAULT now()
);


ALTER TABLE public.direct_message OWNER TO scv;

--
-- Name: direct_message_message_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.direct_message_message_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.direct_message_message_id_seq OWNER TO scv;

ALTER SEQUENCE public.direct_message_message_id_seq OWNED BY public.direct_message.message_id;

ALTER TABLE ONLY public.direct_message ALTER COLUMN message_id SET DEFAULT nextval('public.direct_message_message_id_seq'::regclass);

ALTER TABLE ONLY public.direct_message
    ADD CONSTRAINT direct_message_pkey PRIMARY KEY (message_id);

ALTER TABLE ONLY public.direct_message
    ADD CONSTRAINT direct_message_sender_fkey FOREIGN KEY (sender) REFERENCES public."user"(username);

ALTER TABLE ONLY public.direct_message
    ADD CONSTRAINT direct_message_receiver_fkey FOREIGN KEY (receiver) REFERENCES public."user"(username);

CREATE INDEX idx_direct_message_sender ON public.direct_message(sender);
CREATE INDEX idx_direct_message_receiver ON public.direct_message(receiver);
CREATE INDEX idx_direct_message_sent_at ON public.direct_message(sent_at);

--
-- Name: message_attachments; Type: TABLE; Schema: public; Owner: scv
--

CREATE TABLE public.message_attachments (
    id integer NOT NULL,
    message_id integer NOT NULL,
    link text NOT NULL
);


ALTER TABLE public.message_attachments OWNER TO scv;

--
-- Name: message_attachments_id_seq; Type: SEQUENCE; Schema: public; Owner: scv
--

CREATE SEQUENCE public.message_attachments_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.message_attachments_id_seq OWNER TO scv;

ALTER SEQUENCE public.message_attachments_id_seq OWNED BY public.message_attachments.id;

ALTER TABLE ONLY public.message_attachments ALTER COLUMN id SET DEFAULT nextval('public.message_attachments_id_seq'::regclass);

ALTER TABLE ONLY public.message_attachments
    ADD CONSTRAINT message_attachments_pkey PRIMARY KEY (id);

ALTER TABLE ONLY public.message_attachments
    ADD CONSTRAINT message_attachments_message_id_fkey FOREIGN KEY (message_id) REFERENCES public.direct_message(message_id) ON DELETE CASCADE;

CREATE INDEX idx_message_attachments_message_id ON public.message_attachments(message_id);

--
-- PostgreSQL database dump complete
--

\unrestrict DcZgZcQEi4f3bvxPbX2ujwdl1hqpF6E4scmb22IrRXAOOfLe0MsHhy1DhNbFZEJ

