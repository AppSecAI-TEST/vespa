// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.jdisc.http.filter;

import static org.testng.AssertJUnit.assertTrue;

import java.net.InetSocketAddress;
import java.net.URI;
import java.util.Collections;

import java.util.List;

import com.yahoo.jdisc.Request;
import org.testng.Assert;
import org.testng.annotations.Test;

import com.yahoo.jdisc.http.Cookie;
import com.yahoo.jdisc.http.HttpRequest;
import com.yahoo.jdisc.http.HttpResponse;
import com.yahoo.jdisc.test.TestDriver;

public class DiscFilterResponseTest {

    private static HttpRequest newRequest(URI uri, HttpRequest.Method method, HttpRequest.Version version) {
        InetSocketAddress address = new InetSocketAddress("localhost", 69);
        TestDriver driver = TestDriver.newSimpleApplicationInstanceWithoutOsgi();
        driver.activateContainer(driver.newContainerBuilder());
        HttpRequest request = HttpRequest.newServerRequest(driver, uri, method, version, address);
        request.release();
        assertTrue(driver.close());
        return request;
    }

    public static HttpResponse newResponse(Request request, int status) {
        return HttpResponse.newInstance(status);
    }

    @Test
    public void testGetSetStatus() {
        HttpRequest request = newRequest(URI.create("http://localhost:8080/echo"),
                HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        DiscFilterResponse response = new JdiscFilterResponse(HttpResponse.newInstance(HttpResponse.Status.OK));

        Assert.assertEquals(response.getStatus(), HttpResponse.Status.OK);
        response.setStatus(HttpResponse.Status.REQUEST_TIMEOUT);
        Assert.assertEquals(response.getStatus(), HttpResponse.Status.REQUEST_TIMEOUT);
    }

    @Test
    public void testAttributes() {
        HttpRequest request = newRequest(URI.create("http://localhost:8080/echo"),
                HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        DiscFilterResponse response = new JdiscFilterResponse(HttpResponse.newInstance(HttpResponse.Status.OK));
        response.setAttribute("attr_1", "value1");
        Assert.assertEquals(response.getAttribute("attr_1"), "value1");
        List<String> list = Collections.list(response.getAttributeNames());
        Assert.assertEquals(list.get(0), "attr_1");
        response.removeAttribute("attr_1");
        Assert.assertNull(response.getAttribute("attr_1"));
    }

    @Test
    public void testAddHeader() {
        HttpRequest request = newRequest(URI.create("http://localhost:8080/echo"),
                HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        DiscFilterResponse response = new JdiscFilterResponse(HttpResponse.newInstance(HttpResponse.Status.OK));
        response.addHeader("header1", "value1");
        Assert.assertEquals(response.getHeader("header1"), "value1");
    }

    @Test
    public void testAddCookie() {
        URI uri = URI.create("http://example.com/test");
        HttpRequest httpReq = newRequest(uri, HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        HttpResponse httpResp = newResponse(httpReq, 200);
        DiscFilterResponse response = new JdiscFilterResponse(httpResp);
        response.addCookie(JDiscCookieWrapper.wrap(new Cookie("name", "value")));

        List<Cookie> cookies = response.getCookies();
        Assert.assertEquals(cookies.size(),1);
        Assert.assertEquals(cookies.get(0).getName(),"name");
    }

    @Test
    public void testSetCookie() {
        URI uri = URI.create("http://example.com/test");
        HttpRequest httpReq = newRequest(uri, HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        HttpResponse httpResp = newResponse(httpReq, 200);
        DiscFilterResponse response = new JdiscFilterResponse(httpResp);
        response.setCookie("name", "value");
        List<Cookie> cookies = response.getCookies();
        Assert.assertEquals(cookies.size(),1);
        Assert.assertEquals(cookies.get(0).getName(),"name");

    }

    @Test
    public void testSetHeader() {
        URI uri = URI.create("http://example.com/test");
        HttpRequest httpReq = newRequest(uri, HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        HttpResponse httpResp = newResponse(httpReq, 200);
        DiscFilterResponse response = new JdiscFilterResponse(httpResp);
        response.setHeader("name", "value");
        Assert.assertEquals(response.getHeader("name"), "value");
    }

    @Test
    public void testGetParentResponse() {
        URI uri = URI.create("http://example.com/test");
        HttpRequest httpReq = newRequest(uri, HttpRequest.Method.GET, HttpRequest.Version.HTTP_1_1);
        HttpResponse httpResp = newResponse(httpReq, 200);
        DiscFilterResponse response = new JdiscFilterResponse(httpResp);
        Assert.assertSame(response.getParentResponse(), httpResp);
    }

}
